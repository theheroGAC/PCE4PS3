#include "ps3_platform.h"

#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <sys/event_queue.h>

#include <string.h>
#include <stdlib.h>

#include "pce.h"
#include "sound.h"

extern void WriteBuffer(char *buf, int ch, unsigned dwSize);
extern void write_adpcm(void);

extern SInt32 smode;
extern SChar silent;

static u32              g_port = 0xFFFFFFFFu;
static audioPortConfig   g_port_cfg;
static sys_ppu_thread_t g_audio_tid = 0;
static sys_event_queue_t g_audio_eq = 0;
static sys_ipc_key_t     g_audio_eq_key = 0;
static volatile int     g_audio_running = 0;
static volatile int     g_audio_paused  = 1;
static int              g_audio_inited  = 0;

#define HUGO_INTERNAL_RATE 22050
#define RESAMP_STEP_FP     ((HUGO_INTERNAL_RATE << 16) / PS3_AUDIO_RATE)

static void generate_stereo_block(float *dst_interleaved, size_t frames)
{
    if (g_audio_paused || !host.sound.stereo || !sbuf[0] || !adpcmbuf) {
        memset(dst_interleaved, 0, frames * 2 * sizeof(float));
        return;
    }

    size_t src_frames = (frames * HUGO_INTERNAL_RATE) / PS3_AUDIO_RATE + 2;
    if (src_frames > 256) src_frames = 256;
    unsigned byte_len = (unsigned)(src_frames * 2);

    for (int ch = 0; ch < 6; ch++) {
        if (sbuf[ch]) {
            WriteBuffer((char *)sbuf[ch], ch, byte_len);
        }
    }
    if (adpcmbuf) {
        write_adpcm();
    }

    int32_t lvol = (int32_t)((io.psg_volume >> 4) & 0x0F);
    int32_t rvol = (int32_t)(io.psg_volume & 0x0F);
    if (lvol == 0 && rvol == 0) {
        lvol = 15;
        rvol = 15;
    }

    int16_t psg_stereo[256 * 2];
    for (size_t i = 0; i < src_frames; i++) {
        size_t li = i * 2;
        size_t ri = i * 2 + 1;
        int32_t sum_l = (int8_t)sbuf[0][li] + (int8_t)sbuf[1][li] + (int8_t)sbuf[2][li] +
                        (int8_t)sbuf[3][li] + (int8_t)sbuf[4][li] + (int8_t)sbuf[5][li] +
                        (int8_t)adpcmbuf[li];
        int32_t sum_r = (int8_t)sbuf[0][ri] + (int8_t)sbuf[1][ri] + (int8_t)sbuf[2][ri] +
                        (int8_t)sbuf[3][ri] + (int8_t)sbuf[4][ri] + (int8_t)sbuf[5][ri] +
                        (int8_t)adpcmbuf[ri];
        int32_t sl = sum_l * (lvol + 1) * 16;
        int32_t sr = sum_r * (rvol + 1) * 16;
        if (sl >  32767) sl =  32767;
        if (sl < -32768) sl = -32768;
        if (sr >  32767) sr =  32767;
        if (sr < -32768) sr = -32768;
        psg_stereo[li] = (int16_t)sl;
        psg_stereo[ri] = (int16_t)sr;
    }

    int16_t cdda_48k[PS3_AUDIO_BLOCK * 2];
    memset(cdda_48k, 0, sizeof(cdda_48k));
    if (ps3_cue_is_loaded()) {
        ps3_cue_mix_cdda(cdda_48k, frames);
    }

    float vol = (float)g_cfg.audio_volume / 100.0f;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    const float inv_32768 = vol * (1.0f / 32768.0f);

    uint32_t pos_fp = 0;
    for (size_t i = 0; i < frames; i++) {
        size_t idx = (pos_fp >> 16);
        if (idx >= src_frames) idx = src_frames - 1;
        int32_t l = (int32_t)psg_stereo[idx * 2 + 0] + (int32_t)cdda_48k[i * 2 + 0];
        int32_t r = (int32_t)psg_stereo[idx * 2 + 1] + (int32_t)cdda_48k[i * 2 + 1];
        if (l >  32767) l =  32767;
        if (l < -32768) l = -32768;
        if (r >  32767) r =  32767;
        if (r < -32768) r = -32768;
        dst_interleaved[i * 2 + 0] = (float)l * inv_32768;
        dst_interleaved[i * 2 + 1] = (float)r * inv_32768;
        pos_fp += RESAMP_STEP_FP;
    }
}

static void audio_thread_entry(void *arg)
{
    (void)arg;
    float *ring = (float *)(uintptr_t)g_port_cfg.audioDataStart;
    u32 write_block = 0;
    sys_event_t event;

    while (g_audio_running) {
        int ret = sysEventQueueReceive(g_audio_eq, &event, 25000);
        if (ret == 0 && g_audio_running) {
            float *dst = ring + (size_t)write_block * PS3_AUDIO_BLOCK * 2;
            generate_stereo_block(dst, PS3_AUDIO_BLOCK);
            write_block = (write_block + 1) % AUDIO_BLOCK_8;
        }
    }
    sysThreadExit(0);
}

int ps3_audio_init(void)
{
    if (g_audio_inited) return 0;

    for (int ch = 0; ch < 6; ch++) {
        if (!sbuf[ch]) {
            sbuf[ch] = (char *)calloc(1, SBUF_SIZE_BYTE);
        }
    }
    if (!adpcmbuf) {
        adpcmbuf = (char *)calloc(1, SBUF_SIZE_BYTE);
    }

    smode = 1;
    silent = 0;

    if (audioInit() != 0) {
        ps3_log("audioInit failed\n");
        return -1;
    }

    audioPortParam param;
    memset(&param, 0, sizeof(param));
    param.numChannels = AUDIO_PORT_2CH;
    param.numBlocks   = AUDIO_BLOCK_8;
    param.attrib      = AUDIO_PORT_INITLEVEL;
    param.level       = 1.0f;

    if (audioPortOpen(&param, &g_port) != 0) {
        ps3_log("audioPortOpen failed\n");
        audioQuit();
        return -1;
    }

    if (audioGetPortConfig(g_port, &g_port_cfg) != 0) {
        ps3_log("audioGetPortConfig failed\n");
        audioPortClose(g_port);
        audioQuit();
        return -1;
    }

    if (audioCreateNotifyEventQueue(&g_audio_eq, &g_audio_eq_key) != 0) {
        ps3_log("audioCreateNotifyEventQueue failed\n");
        audioPortClose(g_port);
        audioQuit();
        return -1;
    }

    if (audioSetNotifyEventQueue(g_audio_eq_key) != 0) {
        ps3_log("audioSetNotifyEventQueue failed\n");
        sysEventQueueDestroy(g_audio_eq, 0);
        g_audio_eq = 0;
        audioPortClose(g_port);
        audioQuit();
        return -1;
    }

    float *ring = (float *)(uintptr_t)g_port_cfg.audioDataStart;
    if (ring) {
        memset(ring, 0, AUDIO_BLOCK_8 * PS3_AUDIO_BLOCK * 2 * sizeof(float));
    }
    sysEventQueueDrain(g_audio_eq);

    host.sound.stereo       = 1;
    host.sound.signed_sound = 1;
    host.sound.freq         = HUGO_INTERNAL_RATE;
    host.sound.sample_size  = 1;
    sound_driver            = 1;

    audioPortStart(g_port);
    g_audio_running = 1;
    g_audio_paused  = 1;
    g_audio_inited  = 1;

    sysThreadCreate(&g_audio_tid, audio_thread_entry, NULL,
                    1500, 0x10000, THREAD_JOINABLE, "hugo_audio");

    ps3_log("PS3 cellAudio initialised (port=%u)\n", g_port);
    return 0;
}

void ps3_audio_pause(int paused)
{
    g_audio_paused = paused ? 1 : 0;
}

void ps3_audio_shutdown(void)
{
    if (!g_audio_inited) return;
    g_audio_running = 0;
    if (g_port != 0xFFFFFFFFu) {
        audioPortStop(g_port);
    }
    if (g_audio_tid) {
        u64 retval = 0;
        sysThreadJoin(g_audio_tid, &retval);
        g_audio_tid = 0;
    }
    if (g_port != 0xFFFFFFFFu) {
        if (g_audio_eq_key) {
            audioRemoveNotifyEventQueue(g_audio_eq_key);
            g_audio_eq_key = 0;
        }
        audioPortClose(g_port);
        g_port = 0xFFFFFFFFu;
    }
    if (g_audio_eq) {
        sysEventQueueDestroy(g_audio_eq, 0);
        g_audio_eq = 0;
    }
    audioQuit();
    for (int ch = 0; ch < 6; ch++) {
        if (sbuf[ch]) {
            free(sbuf[ch]);
            sbuf[ch] = NULL;
        }
    }
    if (adpcmbuf) {
        free(adpcmbuf);
        adpcmbuf = NULL;
    }
    g_audio_inited = 0;
}
