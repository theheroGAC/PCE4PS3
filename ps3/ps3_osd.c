#include "ps3_platform.h"

#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sys/systime.h>

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pce.h"
#include "gfx.h"
#include "hard_pce.h"
#include "sound.h"
#include "cheat.h"
#include "optable.h"

UChar *XBuf = NULL;
UChar *osd_gfx_buffer = NULL;
char   short_exe_name[PATH_MAX] = PS3_DATA_ROOT "/";
int    gamepad_driver = 0;

char   hugo_underscore_buffer[2048];
UChar  language = 0;

char          synchro = 0;
unsigned char dump_snd = 0;

UChar         current_freezed_values = 0;
freezed_value list_to_freeze[MAX_FREEZED_VALUE];
char          cdsystem_path[PATH_MAX] = "";
struct hugo_options option;
struct host_machine host;
SDL_Color     olay_cmap[256];

#include "sys_inp.h"

input_config config[16];
UChar        current_config = 0;
char         initial_path[PATH_MAX] = "";

uint16_t htons(uint16_t v)
{
    return v;
}

UChar get_8bit_addr(UInt16 addr)
{
    return Rd6502(addr);
}

void osd_snd_set_volume(UChar v) { (void)v; }
int  osd_snd_init_sound(void)    { return 1; }
void osd_snd_trash_sound(void)   {}

void gpl_pce_cd_handle_write_1800(UInt16 A, UChar V) { (void)A; (void)V; }
UChar gpl_pce_cd_handle_read_1800(UInt16 A)          { (void)A; return 0xFF; }

extern SInt32 smode;
extern SChar silent;

int osd_init_machine(void)
{
    if (!XBuf) {
        XBuf = (UChar *)calloc(XBUF_WIDTH, XBUF_HEIGHT);
    }
    if (XBuf) {
        osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;
    }
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
    host.sound.stereo       = 1;
    host.sound.signed_sound = 1;
    host.sound.freq         = 48000;
    host.sound.sample_size  = 1;
    sound_driver            = 1;
    SetPalette();
    return 1;
}

void osd_shut_machine(void)
{
    if (XBuf) {
        free(XBuf);
        XBuf = NULL;
        osd_gfx_buffer = NULL;
    }
}

SChar osd_keypressed(void)
{
    return 0;
}

UInt16 osd_readkey(void)
{
    return 0;
}

void osd_fix_filename_slashes(char *s)
{
    if (!s) return;
    while (*s) {
        if (*s == '\\') *s = '/';
        s++;
    }
}

void osd_init_paths(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    snprintf(short_exe_name, sizeof(short_exe_name), "%s/", PS3_DATA_ROOT);
    snprintf(sav_basepath, sizeof(sav_basepath), "%s/", PS3_SAVES_DIR);
    snprintf(tmp_basepath, sizeof(tmp_basepath), "%s/tmp/", PS3_DATA_ROOT);
    snprintf(video_path, sizeof(video_path), "%s/shots/", PS3_DATA_ROOT);
    snprintf(log_filename, sizeof(log_filename), "%s", PS3_LOG_PATH);
}

static int osd_gfx_init(void);
static int osd_gfx_init_normal_mode(void);
static void osd_gfx_put_image_normal(void);
static void osd_gfx_shut_normal_mode(void);

osd_gfx_driver osd_gfx_driver_list[3] = {
    { osd_gfx_init, osd_gfx_init_normal_mode, osd_gfx_put_image_normal, osd_gfx_shut_normal_mode },
    { osd_gfx_init, osd_gfx_init_normal_mode, osd_gfx_put_image_normal, osd_gfx_shut_normal_mode },
    { osd_gfx_init, osd_gfx_init_normal_mode, osd_gfx_put_image_normal, osd_gfx_shut_normal_mode }
};

static int osd_gfx_init(void)
{
    if (!XBuf) {
        XBuf = (UChar *)calloc(XBUF_WIDTH, XBUF_HEIGHT);
    }
    if (XBuf) {
        osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;
    }
    SetPalette();
    return 1;
}

static int osd_gfx_init_normal_mode(void)
{
    if (!XBuf) {
        XBuf = (UChar *)calloc(XBUF_WIDTH, XBUF_HEIGHT);
    }
    if (XBuf) {
        osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;
    }
    return 1;
}

static void osd_gfx_shut_normal_mode(void)
{
}

void osd_gfx_set_color(UChar index, UChar r, UChar g, UChar b)
{
    r = (UChar)((r << 2) | (r >> 4));
    g = (UChar)((g << 2) | (g >> 4));
    b = (UChar)((b << 2) | (b >> 4));
    ps3_video_set_palette_entry(index, r, g, b);
}

void osd_gfx_set_message(char *mess)
{
    (void)mess;
}

static void osd_gfx_put_image_normal(void)
{
    int w = io.screen_w > 0 ? io.screen_w : 256;
    int h = io.screen_h > 0 ? io.screen_h : 224;
    if (!XBuf) {
        osd_init_machine();
    }
    if (XBuf) {
        osd_gfx_buffer = XBuf + 32 + 64 * XBUF_WIDTH;
    }

    ps3_video_present_game_frame(w, h);
    ps3_check_sysutil();
    if (g_take_screenshot_pending) {
        g_take_screenshot_pending = 0;
        ps3_take_screenshot();
    }
    ps3_frame_hook();
}

int osd_init_input(void)
{
    return 0;
}

void osd_shutdown_input(void)
{
}

int osd_keyboard(void)
{
    static unsigned int s_ff_div = 0;
    if (!g_fast_forward_active || (s_ff_div++ & 3u) == 0) {
        ps3_check_sysutil();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                ps3_return_to_xmb();
            }
        }
    }

    if (g_app_should_quit) {
        UPeriod = 0;
        return 1;
    }

    unsigned char pads[10];
    memset(pads, 0, sizeof(pads));
    ps3_pad_poll_game(pads);
    for (int i = 0; i < 10; i++) {
        io.JOY[i] = pads[i];
    }

    if (ps3_pad_ingame_combo_triggered()) {
        UPeriod = 0;
        ps3_audio_pause(1);
        int quit = ps3_ingame_menu_run();
        ps3_audio_pause(0);
        if (quit) {
            g_emu_quit_to_browser = 1;
            return 1;
        }
    }

    if (g_fast_forward_active) {
        int spd = g_cfg.fast_forward_speed;
        if (spd < 2) spd = 2;
        if (spd > 5) spd = 5;
        UPeriod = spd - 1;
        interrupt_60hz();
    } else {
        UPeriod = 0;
    }

    return (g_emu_quit_to_browser || g_app_should_quit) ? 1 : 0;
}

void osd_cd_read(UChar *p, UInt32 s)
{
    if (ps3_cue_is_loaded()) {
        ps3_cue_read_sector(p, s);
    } else {
        memset(p, 0, 2048);
    }
}

void osd_cd_stop_audio(void)
{
    ps3_cue_stop_cdda();
}

void osd_cd_play_audio_track(UChar track)
{
    ps3_cue_play_cdda_track(track, 0);
}

void osd_cd_play_audio_range(UChar min_from, UChar sec_from, UChar fra_from,
                             UChar min_to,   UChar sec_to,   UChar fra_to)
{
    (void)min_from; (void)sec_from; (void)fra_from;
    (void)min_to;   (void)sec_to;   (void)fra_to;
}

void osd_cd_nb_tracks(int *first, int *last)
{
    if (first) *first = 1;
    if (last)  *last  = (int)(nb_max_track > 0 ? nb_max_track : 1);
}

void osd_cd_length(int *min, int *sec, int *fra)
{
    if (min) *min = 60;
    if (sec) *sec = 0;
    if (fra) *fra = 0;
}

void osd_cd_track_info(UChar track, int *min, int *sec, int *fra, int *control)
{
    if (track < 1) track = 1;
    if (min)     *min     = CD_track[track].beg_min;
    if (sec)     *sec     = CD_track[track].beg_sec;
    if (fra)     *fra     = CD_track[track].beg_fra;
    if (control) *control = CD_track[track].type;
}

void osd_cd_subchannel_info(unsigned short offset)
{
    Wr6502(offset, 0);
}

void osd_cd_status(int *status)
{
    if (status) *status = 0;
}

int osd_cd_init(char *device)
{
    (void)device;
    return 0;
}

void osd_cd_close(void)
{
    ps3_cue_stop_cdda();
}

void osd_cd_resume(void)
{
}

void osd_cd_pause(void)
{
    ps3_cue_stop_cdda();
}
