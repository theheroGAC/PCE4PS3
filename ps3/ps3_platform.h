#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include "ps3_btn_icons.h"

#define PS3_APP_NAME        "PCE-PS3"
#define PS3_APP_VERSION     "1.0"
#define PS3_TITLE_ID        "PCE4PS300"

#define PS3_DATA_ROOT       "/dev_hdd0/pce4ps3"
#define PS3_ROMS_DIR        "/dev_hdd0/pce4ps3/roms"
#define PS3_BIOS_DIR        "/dev_hdd0/pce4ps3/bios"
#define PS3_SAVES_DIR       "/dev_hdd0/pce4ps3/saves"
#define PS3_BRAM_DIR        "/dev_hdd0/pce4ps3/bram"
#define PS3_CONFIG_PATH     "/dev_hdd0/pce4ps3/hugo.ini"
#define PS3_LOG_PATH        "/dev_hdd0/pce4ps3/hugo.log"

#define PS3_PKG_USRDIR      "/dev_hdd0/game/" PS3_TITLE_ID "/USRDIR"
#define PS3_SKINS_DIR       PS3_PKG_USRDIR "/skins"

#define PS3_USB_ROMS_FMT    "/dev_usb00%d/roms/pce"
#define PS3_USB_ROOT_FMT    "/dev_usb00%d/pce"
#define PS3_USB_ALT_FMT     "/dev_usb00%d/pce4ps3/roms"

#define PS3_SCREEN_W        1280
#define PS3_SCREEN_H        720

#define PS3_THUMB_W         136
#define PS3_THUMB_H         114

#define PS3_AUDIO_RATE      48000
#define PS3_AUDIO_BLOCK     256

#define PS3_ACT_UP          (1u << 0)
#define PS3_ACT_DOWN        (1u << 1)
#define PS3_ACT_LEFT        (1u << 2)
#define PS3_ACT_RIGHT       (1u << 3)
#define PS3_ACT_CONFIRM     (1u << 4)
#define PS3_ACT_CANCEL      (1u << 5)
#define PS3_ACT_ALT1        (1u << 6)
#define PS3_ACT_ALT2        (1u << 7)
#define PS3_ACT_L1          (1u << 8)
#define PS3_ACT_R1          (1u << 9)
#define PS3_ACT_L2          (1u << 10)
#define PS3_ACT_R2          (1u << 11)
#define PS3_ACT_START       (1u << 12)
#define PS3_ACT_SELECT      (1u << 13)
#define PS3_ACT_MENU        (1u << 14)

enum {
    PS3_ASPECT_4_3 = 0,
    PS3_ASPECT_PIXEL,
    PS3_ASPECT_16_9,
    PS3_ASPECT_COUNT
};

enum {
    PS3_FILTER_NEAREST = 0,
    PS3_FILTER_LINEAR,
    PS3_FILTER_COUNT
};

enum {
    PS3_ICON_CROSS = 0,
    PS3_ICON_CIRCLE = 1,
    PS3_ICON_SQUARE = 2,
    PS3_ICON_TRIANGLE = 3
};

enum {
    PCE_MEDIA_HUCARD = 0,
    PCE_MEDIA_SGX = 1,
    PCE_MEDIA_CDROM = 2,
    PCE_MEDIA_ZIP = 3
};

typedef struct {
    int aspect;
    int filter;
    int scanlines;
    int swap_i_ii;
    int turbo_rate;
    int audio_volume;
    int show_fps;
    char bios_name[256];
    char last_rom_dir[512];
} ps3_config_t;

extern ps3_config_t g_cfg;
extern volatile int g_xmb_menu_open;

void ps3_log_init(void);
void ps3_log_close(void);
void ps3_log(const char *fmt, ...);

void ps3_ensure_directories(void);
int  ps3_find_syscard(char *out_path, size_t cap);

void ps3_config_defaults(void);
void ps3_config_load(void);
void ps3_config_save(void);

int  ps3_video_init(void);
void ps3_video_shutdown(void);
void ps3_video_apply_filter(void);
void ps3_video_set_palette_entry(unsigned char idx,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b);
void ps3_video_render_game_to_fb(int src_w, int src_h);
void ps3_video_present_game_frame(int src_w, int src_h);
void ps3_video_capture_thumb(uint32_t *dst_argb, int dst_w, int dst_h);
void ps3_video_toast(const char *msg, int frames);

void ps3_video_clear(uint8_t r, uint8_t g, uint8_t b);
void ps3_video_present(void);
void ps3_ui_rect(int x, int y, int w, int h,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void ps3_ui_outline(int x, int y, int w, int h,
                    uint8_t r, uint8_t g, uint8_t b);
int  ps3_ui_text(int x, int y, const char *text,
                 uint8_t r, uint8_t g, uint8_t b, int size_tier);
int  ps3_ui_text_width(const char *text, int size_tier);
void ps3_ui_button_icon(int which, int x, int y, int size);
void ps3_ui_header_bar(const char *title, const char *subtitle);
void ps3_ui_footer_bar(const char *hints);

void ps3_draw_rect(int x, int y, int w, int h, uint32_t argb);
void ps3_draw_panel(int x, int y, int w, int h, uint32_t top_argb, uint32_t bot_argb, uint32_t border_argb);
void ps3_draw_bevel_box(int x, int y, int w, int h, uint32_t fill_argb, uint32_t hi_argb, uint32_t lo_argb);
void ps3_draw_border(int x, int y, int w, int h, uint32_t argb);
void ps3_draw_hline(int x, int y, int w, uint32_t argb);
void ps3_draw_card_row(int x, int y, int w, int h, int selected);
int  ps3_draw_badge(int x, int y, const char *label, uint32_t bg_argb, uint32_t fg_argb);
void ps3_draw_selector_row(int x, int y, int w, int h, const char *label, const char *value_str, int selected);
void ps3_draw_switch_row(int x, int y, int w, int h, const char *label, int is_on, int selected);
void ps3_draw_slider_row(int x, int y, int w, int h, const char *label, int pct, const char *val_str, int selected);
void ps3_draw_tab_bar(int y, const char **tabs, int tab_count, int active_tab);
int  ps3_draw_btn_icon(int x, int y, int btn_type);
int  ps3_draw_btn_label(int x, int y, int btn_type, const char *label, uint32_t text_argb);
void ps3_draw_text(int x, int y, uint32_t argb, int scale, const char *text);
void ps3_draw_text_shadow(int x, int y, uint32_t fg_argb, int scale, const char *text);
void ps3_draw_text_centered(int y, uint32_t argb, int scale, const char *text);
void ps3_draw_text_clipped(int x, int y, int max_w, uint32_t argb, int scale, const char *text);
int  ps3_text_width(int scale, const char *text);
void ps3_draw_pce_background(const char *subtitle_right);
void ps3_draw_footer_buttons(const int *btns, const char **labels, int count);
void ps3_draw_media_card(int x, int y, int w, int h, const char *title, int media_type, const char *badge_str);
void ps3_draw_thumb_box(int x, int y, int w, int h, const uint32_t *pixels, int pw, int ph, const char *empty_label);
void ps3_draw_boot_splash(const char *status_line, int step_pct);
void ps3_show_error_dialog(const char *title, const char *msg1, const char *msg2);
int  ps3_validate_pce_file(const char *path);

int  ps3_audio_init(void);
void ps3_audio_shutdown(void);
void ps3_audio_pause(int paused);

uint32_t ps3_pad_poll_menu(uint32_t *out_pressed);
void     ps3_pad_poll_game(unsigned char joy_out[5]);
int      ps3_pad_ingame_combo_triggered(void);

void ps3_frame_hook(void);
int  ps3_ingame_menu_run(void);
int  osd_init_machine(void);
void osd_shut_machine(void);
extern volatile int g_emu_quit_to_browser;
extern volatile int g_app_should_quit;
void ps3_return_to_xmb(void);

void ps3_bram_load(const char *rom_base);
void ps3_bram_save(const char *rom_base);
int  ps3_state_save(int slot);
int  ps3_state_load(int slot);
int  ps3_state_exists(int slot);
int  ps3_state_delete(int slot);
int  ps3_state_load_thumb(int slot, uint32_t *out_argb);

int  ps3_cue_load(const char *cue_path);
void ps3_cue_unload(void);
int  ps3_cue_is_loaded(void);
void ps3_cue_read_sector(unsigned char *buf, uint32_t lba);
int  ps3_cue_play_cdda_track(unsigned char track, int repeat);
void ps3_cue_stop_cdda(void);
void ps3_cue_mix_cdda(int16_t *stereo_accum, size_t frames);

#endif
