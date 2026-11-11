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

#define SND_RATE          PS3_AUDIO_RATE
#define SND_RING_FRAMES   16384u
#define SND_RING_MASK     (SND_RING_FRAMES - 1u)
#define SND_BLOCK_FRAMES  PS3_AUDIO_BLOCK
#define SND_TARGET        2048u
#define SND_PUSH_MAX      2048

static int16_t          s_ring[SND_RING_FRAMES * 2];
static volatile unsigned s_wr = 0;
static volatile unsigned s_rd = 0;
static volatile int      s_stopped = 1;

static u32               g_port = 0xFFFFFFFFu;
static audioPortConfig   g_port_cfg;
static sys_ppu_thread_t  g_audio_tid = 0;
static sys_event_queue_t g_audio_eq = 0;
static sys_ipc_key_t     g_audio_eq_key = 0;
static volatile int      g_audio_running = 0;
static int               g_audio_inited = 0;

static void audio_fill_block(float *out)
{
    unsigned rp, wp, used, n, i;

    if (!out) return;

    if (s_stopped) {
        memset(out, 0, (size_t)SND_BLOCK_FRAMES * 2 * sizeof(float));
        return;
    }

    wp = s_wr;
    rp = s_rd;
    used = wp - rp;

    if (used == 0) {
        memset(out, 0, (size_t)SND_BLOCK_FRAMES * 2 * sizeof(float));
        return;
    }

    n = used < SND_BLOCK_FRAMES ? used : SND_BLOCK_FRAMES;

    if (used > SND_TARGET + 512) rp += 2;
    else if (used + 512 < SND_TARGET && n > 2) n -= 2;

    for (i = 0; i < n; i++) {
        unsigned idx = (rp + i) & SND_RING_MASK;
        out[i * 2 + 0] = (float)s_ring[idx * 2 + 0] * (1.0f / 32768.0f);
        out[i * 2 + 1] = (float)s_ring[idx * 2 + 1] * (1.0f / 32768.0f);
    }

    if (n < SND_BLOCK_FRAMES) {
        for (i = n; i < SND_BLOCK_FRAMES; i++) {
            out[i * 2 + 0] = 0.0f;
            out[i * 2 + 1] = 0.0f;
        }
    }

    __sync_synchronize();
    s_rd = rp + n;
}

static void audio_thread_entry(void *arg)
{
    (void)arg;
    float *ring = (float *)(uintptr_t)g_port_cfg.audioDataStart;
    u32 write_block = 0;
    sys_event_t event;

    if (!ring) sysThreadExit(0);

    while (g_audio_running) {
        int ret = sysEventQueueReceive(g_audio_eq, &event, 25000);
        if (ret == 0 && g_audio_running) {
            float *dst = ring + (size_t)write_block * SND_BLOCK_FRAMES * 2;
            audio_fill_block(dst);
            write_block = (write_block + 1) % AUDIO_BLOCK_8;
        }
    }
    sysThreadExit(0);
}

void ps3_audio_push_frame(int frames)
{
    static int16_t psg_stereo[SND_PUSH_MAX * 2];
    static int16_t cdda_stereo[SND_PUSH_MAX * 2];
    unsigned wp, used;
    int total;

    if (!g_audio_inited || s_stopped || frames <= 0) return;

    static int s_was_ff = 0;
    if (g_fast_forward_active) {
        s_was_ff = 1;
        for (int ch = 0; ch < 6; ch++) {
            io.psg_da_count[ch] = 0;
            io.psg_da_index[ch] = 0;
        }
        return;
    }
    if (s_was_ff) {
        s_was_ff = 0;
        s_rd = s_wr;
        for (int ch = 0; ch < 6; ch++) {
            io.psg_da_count[ch] = 0;
            io.psg_da_index[ch] = 0;
        }
    }
    if (!sbuf[0] || !adpcmbuf) return;
    if (frames > SND_PUSH_MAX) frames = SND_PUSH_MAX;

    total = frames;

    unsigned byte_len = (unsigned)(total * 2);

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

    for (int k = 0; k < total; k++) {
        size_t li = (size_t)k * 2;
        size_t ri = (size_t)k * 2 + 1;
        int32_t sum_l = (int8_t)sbuf[0][li] + (int8_t)sbuf[1][li] + (int8_t)sbuf[2][li] +
                        (int8_t)sbuf[3][li] + (int8_t)sbuf[4][li] + (int8_t)sbuf[5][li] +
                        (int8_t)adpcmbuf[li];
        int32_t sum_r = (int8_t)sbuf[0][ri] + (int8_t)sbuf[1][ri] + (int8_t)sbuf[2][ri] +
                        (int8_t)sbuf[3][ri] + (int8_t)sbuf[4][ri] + (int8_t)sbuf[5][ri] +
                        (int8_t)adpcmbuf[ri];
        int32_t sl = (sum_l * (lvol + 1) * 5) / 2;
        int32_t sr = (sum_r * (rvol + 1) * 5) / 2;
        if (sl >  32767) sl =  32767;
        if (sl < -32768) sl = -32768;
        if (sr >  32767) sr =  32767;
        if (sr < -32768) sr = -32768;
        psg_stereo[li] = (int16_t)sl;
        psg_stereo[ri] = (int16_t)sr;
    }

    memset(cdda_stereo, 0, (size_t)total * 2 * sizeof(int16_t));
    if (ps3_cue_is_loaded()) {
        ps3_cue_mix_cdda(cdda_stereo, (size_t)total);
    }

    int vol = g_cfg.audio_volume;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    wp = s_wr;
    used = wp - s_rd;
    if (used >= SND_RING_FRAMES - 64u) return;
    if (used + (unsigned)total > SND_RING_FRAMES) {
        total = (int)(SND_RING_FRAMES - used);
    }
    if (total <= 0) return;

    for (int k = 0; k < total; k++) {
        int32_t l = (int32_t)psg_stereo[k * 2 + 0] + (int32_t)cdda_stereo[k * 2 + 0];
        int32_t r = (int32_t)psg_stereo[k * 2 + 1] + (int32_t)cdda_stereo[k * 2 + 1];
        if (l >  32767) l =  32767;
        if (l < -32768) l = -32768;
        if (r >  32767) r =  32767;
        if (r < -32768) r = -32768;
        l = (l * vol) / 100;
        r = (r * vol) / 100;
        {
            unsigned idx = (wp + (unsigned)k) & SND_RING_MASK;
            s_ring[idx * 2 + 0] = (int16_t)l;
            s_ring[idx * 2 + 1] = (int16_t)r;
        }
    }

    __sync_synchronize();
    s_wr = wp + (unsigned)total;
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

    memset(s_ring, 0, sizeof(s_ring));
    s_wr = 0;
    s_rd = 0;
    s_stopped = 1;

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
        memset(ring, 0, (size_t)AUDIO_BLOCK_8 * SND_BLOCK_FRAMES * 2 * sizeof(float));
    }
    sysEventQueueDrain(g_audio_eq);

    host.sound.stereo       = 1;
    host.sound.signed_sound = 1;
    host.sound.freq         = SND_RATE;
    host.sound.sample_size  = 1;
    sound_driver            = 1;

    g_audio_running = 1;
    g_audio_inited  = 1;

    if (sysThreadCreate(&g_audio_tid, audio_thread_entry, NULL,
                        1000, 0x10000, THREAD_JOINABLE, "hugo_audio") != 0) {
        ps3_log("audio thread create failed\n");
        g_audio_running = 0;
        g_audio_inited  = 0;
        audioRemoveNotifyEventQueue(g_audio_eq_key);
        sysEventQueueDestroy(g_audio_eq, 0);
        g_audio_eq = 0;
        audioPortClose(g_port);
        audioQuit();
        return -1;
    }

    audioPortStart(g_port);
    s_stopped = 0;

    ps3_log("PS3 cellAudio ring active (port=%u, rate=%d)\n", g_port, SND_RATE);
    return 0;
}

void ps3_audio_pause(int paused)
{
    if (paused) {
        s_stopped = 1;
    } else {
        s_rd = s_wr;
        s_stopped = 0;
    }
}

void ps3_audio_shutdown(void)
{
    if (!g_audio_inited) return;

    s_stopped = 1;
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
