#include "ps3_platform.h"
#include "ps3_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sys/process.h>

#include "pce.h"
#include "hard_pce.h"
#include "sound.h"
#include "iniconfig.h"

SYS_PROCESS_PARAM(1001, 0x100000);

volatile int g_emu_quit_to_browser = 0;
volatile int g_app_should_quit     = 0;

static char g_rom_base[256]      = "default";
static char g_active_rom_path[512] = "";
static int  g_quick_slot         = 0;
static int  g_game_loaded        = 0;

static uint32_t g_slot_thumb_argb[PS3_THUMB_W * PS3_THUMB_H];
static int      g_slot_thumb_loaded_slot = -1;
static int      g_slot_thumb_valid       = 0;

static volatile int s_xmb_exit_requested = 0;

static void ps3_sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param;
    (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) {
        s_xmb_exit_requested = 1;
    }
}

void ps3_check_sysutil(void)
{
    sysUtilCheckCallback();
    if (s_xmb_exit_requested) {
        ps3_return_to_xmb();
    }
}

void ps3_return_to_xmb(void)
{
    if (g_game_loaded && g_rom_base[0]) {
        ps3_bram_save(g_rom_base);
    }
    ps3_config_save();
    ps3_audio_shutdown();
    ps3_log_close();
    ioPadEnd();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);
    sysProcessExit(0);
    exit(0);
}

static void extract_rom_basename(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? (slash + 1) : path;
    snprintf(out, cap, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    if (!out[0]) snprintf(out, cap, "game");
}

void ps3_bram_load(const char *rom_base)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.brm", PS3_BRAM_DIR,
             (rom_base && rom_base[0]) ? rom_base : "system");
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (WRAM) {
        fread(WRAM, 1, 0x2000, f);
    }
    fclose(f);
    ps3_log("Loaded BRAM: %s\n", path);
}

void ps3_bram_save(const char *rom_base)
{
    if (!WRAM) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.brm", PS3_BRAM_DIR,
             (rom_base && rom_base[0]) ? rom_base : "system");
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(WRAM, 1, 0x2000, f);
    fclose(f);
    ps3_log("Saved BRAM: %s\n", path);
}

#define PS3_STATE_MAGIC 0x50434553u

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t  pc;
    uint8_t  a, x, y, p, s;
    uint8_t  mmr[8];
    uint8_t  ram[0x8000];
    uint8_t  wram[0x2000];
    uint8_t  vram[0x10000];
    uint16_t spram[64 * 4];
    uint8_t  pal[512];
    IO       vdc_io;
} ps3_savestate_t;

static void build_state_path(int slot, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s.s%02d", PS3_SAVES_DIR, g_rom_base, slot);
}

static void build_thumb_path(int slot, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s.s%02d.thm", PS3_SAVES_DIR, g_rom_base, slot);
}

int ps3_state_exists(int slot)
{
    char path[512];
    struct stat st;
    build_state_path(slot, path, sizeof(path));
    return (stat(path, &st) == 0 && st.st_size > 0) ? 1 : 0;
}

int ps3_state_delete(int slot)
{
    char path[512];
    char tpath[512];
    build_state_path(slot, path, sizeof(path));
    build_thumb_path(slot, tpath, sizeof(tpath));
    unlink(tpath);
    int r = unlink(path);
    if (g_slot_thumb_loaded_slot == slot) {
        g_slot_thumb_loaded_slot = -1;
        g_slot_thumb_valid = 0;
    }
    return r;
}

int ps3_state_load_thumb(int slot, uint32_t *out_argb)
{
    char tpath[512];
    if (!out_argb) return 0;
    build_thumb_path(slot, tpath, sizeof(tpath));
    FILE *f = fopen(tpath, "rb");
    if (!f) return 0;
    size_t want = (size_t)PS3_THUMB_W * PS3_THUMB_H;
    size_t got = fread(out_argb, sizeof(uint32_t), want, f);
    fclose(f);
    return (got == want) ? 1 : 0;
}

static void refresh_cached_slot_thumb(int slot)
{
    if (g_slot_thumb_loaded_slot == slot) return;
    g_slot_thumb_loaded_slot = slot;
    g_slot_thumb_valid = ps3_state_load_thumb(slot, g_slot_thumb_argb);
}

int ps3_state_save(int slot)
{
    char path[512];
    char tpath[512];
    build_state_path(slot, path, sizeof(path));
    build_thumb_path(slot, tpath, sizeof(tpath));

    ps3_savestate_t *st = (ps3_savestate_t *)calloc(1, sizeof(ps3_savestate_t));
    if (!st) return -1;

    st->magic   = PS3_STATE_MAGIC;
    st->version = 1;
    st->pc      = (int32_t)reg_pc;
    st->a       = reg_a;
    st->x       = reg_x;
    st->y       = reg_y;
    st->p       = reg_p;
    st->s       = reg_s;
    memcpy(st->mmr, mmr, 8);
    if (RAM)   memcpy(st->ram,   RAM,   0x8000);
    if (WRAM)  memcpy(st->wram,  WRAM,  0x2000);
    if (VRAM)  memcpy(st->vram,  VRAM,  0x10000);
    if (SPRAM) memcpy(st->spram, SPRAM, sizeof(st->spram));
    if (Pal)   memcpy(st->pal,   Pal,   sizeof(st->pal));
    st->vdc_io = io;

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(st);
        return -1;
    }
    size_t wr = fwrite(st, 1, sizeof(*st), f);
    fclose(f);
    free(st);

    if (wr != sizeof(*st)) return -1;

    uint32_t *thumb = (uint32_t *)malloc((size_t)PS3_THUMB_W * PS3_THUMB_H * sizeof(uint32_t));
    if (thumb) {
        ps3_video_capture_thumb(thumb, PS3_THUMB_W, PS3_THUMB_H);
        FILE *tf = fopen(tpath, "wb");
        if (tf) {
            fwrite(thumb, sizeof(uint32_t), (size_t)PS3_THUMB_W * PS3_THUMB_H, tf);
            fclose(tf);
        }
        free(thumb);
    }

    g_slot_thumb_loaded_slot = -1;
    refresh_cached_slot_thumb(slot);
    return 0;
}

int ps3_state_load(int slot)
{
    char path[512];
    build_state_path(slot, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    ps3_savestate_t *st = (ps3_savestate_t *)malloc(sizeof(ps3_savestate_t));
    if (!st) {
        fclose(f);
        return -1;
    }
    size_t rd = fread(st, 1, sizeof(*st), f);
    fclose(f);

    if (rd != sizeof(*st) || st->magic != PS3_STATE_MAGIC || st->version != 1) {
        free(st);
        return -1;
    }

    reg_pc = (uint16_t)st->pc;
    reg_a  = st->a;
    reg_x  = st->x;
    reg_y  = st->y;
    reg_p  = st->p;
    reg_s  = st->s;
    for (int i = 0; i < 8; i++) {
        bank_set((uint8_t)i, st->mmr[i]);
    }
    if (RAM)   memcpy(RAM,   st->ram,   0x8000);
    if (WRAM)  memcpy(WRAM,  st->wram,  0x2000);
    if (VRAM)  memcpy(VRAM,  st->vram,  0x10000);
    if (SPRAM) memcpy(SPRAM, st->spram, sizeof(st->spram));
    if (Pal)   memcpy(Pal,   st->pal,   sizeof(st->pal));
    io = st->vdc_io;

    if (vchange)  memset(vchange,  0xFF, VRAMSIZE / 32);
    if (vchanges) memset(vchanges, 0xFF, VRAMSIZE / 128);

    free(st);
    return 0;
}

static uint32_t g_prev_act  = 0;
static int      g_rep_timer = 0;
static uint32_t g_turbo_cnt = 0;
static padData  g_last_paddata[MAX_PADS];
static int      g_paddata_inited = 0;

static void reset_paddata_entry(int p)
{
    memset(&g_last_paddata[p], 0, sizeof(padData));
    g_last_paddata[p].ANA_L_H = 0x80;
    g_last_paddata[p].ANA_L_V = 0x80;
    g_last_paddata[p].ANA_R_H = 0x80;
    g_last_paddata[p].ANA_R_V = 0x80;
}

static void ensure_paddata_init(void)
{
    if (g_paddata_inited) return;
    for (int i = 0; i < MAX_PADS; i++) {
        reset_paddata_entry(i);
    }
    g_paddata_inited = 1;
}

uint32_t ps3_pad_poll_menu(uint32_t *out_pressed)
{
    ensure_paddata_init();

    padInfo padinfo;
    padData paddata;
    uint32_t cur = 0;

    if (ioPadGetInfo(&padinfo) == 0) {
        for (int p = 0; p < MAX_PADS; p++) {
            if (!padinfo.status[p]) {
                reset_paddata_entry(p);
                continue;
            }
            if (ioPadGetData(p, &paddata) == 0 && paddata.len >= 8) {
                g_last_paddata[p] = paddata;
            }

            padData *pd = &g_last_paddata[p];
            if (pd->len == 0) continue;

            if (pd->BTN_UP    || pd->ANA_L_V < 0x40) cur |= PS3_ACT_UP;
            if (pd->BTN_DOWN  || pd->ANA_L_V > 0xC0) cur |= PS3_ACT_DOWN;
            if (pd->BTN_LEFT  || pd->ANA_L_H < 0x40) cur |= PS3_ACT_LEFT;
            if (pd->BTN_RIGHT || pd->ANA_L_H > 0xC0) cur |= PS3_ACT_RIGHT;

            if (pd->BTN_CROSS)    cur |= PS3_ACT_CONFIRM;
            if (pd->BTN_CIRCLE)   cur |= PS3_ACT_CANCEL;
            if (pd->BTN_SQUARE)   cur |= PS3_ACT_ALT1;
            if (pd->BTN_TRIANGLE) cur |= PS3_ACT_ALT2;
            if (pd->BTN_L1)       cur |= PS3_ACT_L1;
            if (pd->BTN_R1)       cur |= PS3_ACT_R1;
            if (pd->BTN_L2)       cur |= PS3_ACT_L2;
            if (pd->BTN_R2)       cur |= PS3_ACT_R2;
            if (pd->BTN_START)    cur |= PS3_ACT_START;
            if (pd->BTN_SELECT)   cur |= PS3_ACT_SELECT;
            if ((pd->BTN_L3 && pd->BTN_R3) ||
                (pd->BTN_L2 && pd->BTN_R2) ||
                (pd->BTN_SELECT && pd->BTN_START)) {
                cur |= PS3_ACT_MENU;
            }
            break;
        }
    }

    uint32_t newly = cur & ~g_prev_act;
    uint32_t dirs  = cur & (PS3_ACT_UP | PS3_ACT_DOWN | PS3_ACT_LEFT | PS3_ACT_RIGHT);
    if (dirs && dirs == (g_prev_act & (PS3_ACT_UP | PS3_ACT_DOWN | PS3_ACT_LEFT | PS3_ACT_RIGHT))) {
        g_rep_timer++;
        if (g_rep_timer > 10) {
            if (g_rep_timer > 22) {
                newly |= dirs;
            } else if (g_rep_timer % 2 == 0) {
                newly |= dirs;
            }
        }
    } else {
        g_rep_timer = 0;
    }

    g_prev_act = cur;
    if (out_pressed) *out_pressed = newly;
    return cur;
}

static int g_ingame_combo_flag = 0;

void ps3_pad_poll_game(unsigned char joy_out[10])
{
    ensure_paddata_init();

    padInfo padinfo;
    padData paddata;
    g_turbo_cnt++;
    g_ingame_combo_flag = 0;

    for (int i = 0; i < 10; i++) joy_out[i] = 0x00;

    if (ioPadGetInfo(&padinfo) != 0) return;

    int pad_idx = 0;
    int period = (g_cfg.turbo_rate > 0) ? g_cfg.turbo_rate : 2;
    int turbo_on = ((g_turbo_cnt / (uint32_t)period) & 1u) != 0;

    for (int p = 0; p < MAX_PADS && pad_idx < 5; p++) {
        if (!padinfo.status[p]) {
            reset_paddata_entry(p);
            continue;
        }
        if (ioPadGetData(p, &paddata) == 0 && paddata.len >= 8) {
            g_last_paddata[p] = paddata;
        }

        padData *pd = &g_last_paddata[p];
        if (pd->len == 0) continue;

        if ((pd->BTN_L2 && pd->BTN_R2) ||
            (pd->BTN_L3 && pd->BTN_R3) ||
            (pd->BTN_SELECT && pd->BTN_START)) {
            if (pad_idx == 0) g_ingame_combo_flag = 1;
        }

        if (pad_idx == 0) {
            static int s_r3_prev = 0;
            int r3_now = (pd->BTN_R3 && !pd->BTN_L3);
            if (r3_now && !s_r3_prev) {
                g_take_screenshot_pending = 1;
            }
            s_r3_prev = r3_now;
        }

        unsigned char v = 0x00;
        if (pd->BTN_UP    || pd->ANA_L_V < 0x40) v |= 0x10;
        else if (pd->BTN_DOWN  || pd->ANA_L_V > 0xC0) v |= 0x40;

        if (pd->BTN_LEFT  || pd->ANA_L_H < 0x40) v |= 0x80;
        else if (pd->BTN_RIGHT || pd->ANA_L_H > 0xC0) v |= 0x20;

        if (g_six_button_mode) {
            if (pd->BTN_CROSS)  v |= 0x02;
            if (pd->BTN_CIRCLE) v |= 0x01;

            unsigned char vex = 0x00;
            if (pd->BTN_SQUARE)   vex |= 0x02;
            if (pd->BTN_TRIANGLE) vex |= 0x04;
            if (pd->BTN_R1)       vex |= 0x08;
            if (pd->BTN_L1)       vex |= 0x08;
            if (pd->BTN_R2)       vex |= 0x01;

            if (pd->BTN_SELECT && !pd->BTN_START) v |= 0x04;
            if (pd->BTN_START  && !pd->BTN_SELECT) v |= 0x08;

            joy_out[pad_idx] = v;
            joy_out[pad_idx + 5] = vex;
            pad_idx++;
        } else {
            int btn_i  = g_cfg.swap_i_ii ? (pd->BTN_CROSS  != 0) : (pd->BTN_CIRCLE != 0);
            int btn_ii = g_cfg.swap_i_ii ? (pd->BTN_SQUARE != 0) : (pd->BTN_CROSS  != 0);
            int trb_i  = pd->BTN_R1 != 0 || pd->BTN_TRIANGLE != 0;
            int trb_ii = pd->BTN_L1 != 0 || (g_cfg.swap_i_ii ? (pd->BTN_CIRCLE != 0) : (pd->BTN_SQUARE != 0));

            if (btn_ii || (trb_ii && turbo_on)) v |= 0x01;
            if (btn_i  || (trb_i  && turbo_on)) v |= 0x02;

            if (pd->BTN_SELECT && !pd->BTN_START) v |= 0x04;
            if (pd->BTN_START  && !pd->BTN_SELECT) v |= 0x08;

            joy_out[pad_idx] = v;
            joy_out[pad_idx + 5] = 0x00;
            pad_idx++;
        }
    }
}

int ps3_pad_ingame_combo_triggered(void)
{
    return g_ingame_combo_flag;
}

static const char *aspect_label(int a)
{
    switch (a) {
        case PS3_ASPECT_4_3:   return "4:3 Standard";
        case PS3_ASPECT_PIXEL: return "Pixel-Perfect";
        case PS3_ASPECT_16_9:  return "16:9 Widescreen";
        default:               return "4:3 Standard";
    }
}

static const char *filter_label(int f)
{
    if (f == PS3_FILTER_ANISOTROPIC) return "Anisotropic / Sharp";
    if (f == PS3_FILTER_LINEAR)      return "Bilinear Smooth";
    return "Nearest Crisp";
}

static const char *turbo_label(int rate)
{
    if (rate <= 1) return "Ultra (60 Hz)";
    if (rate == 2) return "Fast (30 Hz)";
    if (rate == 3) return "Medium (20 Hz)";
    return "Slow (15 Hz)";
}

static const char *pad_mode_label(int m)
{
    switch (m) {
        case PS3_PAD_AUTO:    return "Auto (Fighting Games)";
        case PS3_PAD_6BUTTON: return "6-Button Arcade (Avenue Pad 6)";
        case PS3_PAD_2BUTTON: return "2-Button Standard";
        default:              return "Auto (Fighting Games)";
    }
}

static int ps3_is_fighting_game(const char *path, int rom_size)
{
    if (rom_size > 0x80) return 1;
    if (!path || !path[0]) return 0;

    char lower[512];
    int i = 0;
    while (path[i] && i < (int)(sizeof(lower) - 1)) {
        lower[i] = (char)tolower((unsigned char)path[i]);
        i++;
    }
    lower[i] = '\0';

    if (strstr(lower, "street fighter") || strstr(lower, "street_fighter") || strstr(lower, "sf2") || strstr(lower, "sfii") || strstr(lower, "champion edition")) return 1;
    if (strstr(lower, "strip fighter") || strstr(lower, "strip_fighter")) return 1;
    if (strstr(lower, "fatal fury") || strstr(lower, "fatal_fury") || strstr(lower, "garou")) return 1;
    if (strstr(lower, "art of fighting") || strstr(lower, "art_of_fighting") || strstr(lower, "ryuko") || strstr(lower, "ryuuko")) return 1;
    if (strstr(lower, "world heroes") || strstr(lower, "world_heroes")) return 1;
    if (strstr(lower, "flash hiders") || strstr(lower, "flash_hiders")) return 1;
    if (strstr(lower, "advanced v.g") || strstr(lower, "advanced vg") || strstr(lower, "variable geo")) return 1;
    if (strstr(lower, "kabuki") || strstr(lower, "ittouryoudan") || strstr(lower, "itouryou")) return 1;
    if (strstr(lower, "martial champion") || strstr(lower, "martial_champion")) return 1;
    if (strstr(lower, "asuka 120") || strstr(lower, "asuka_120") || strstr(lower, "asuka120")) return 1;
    if (strstr(lower, "algonos")) return 1;
    if (strstr(lower, "battlefield")) return 1;
    if (strstr(lower, "fire pro") || strstr(lower, "fire_pro") || strstr(lower, "joshi")) return 1;
    if (strstr(lower, "linda") || strstr(lower, "linda3")) return 1;
    if (strstr(lower, "godzilla") || strstr(lower, "bakutou")) return 1;
    if (strstr(lower, "sapphire")) return 1;

    return 0;
}

static void run_state_manager_modal(void)
{
    int sel = g_quick_slot;
    g_slot_thumb_loaded_slot = -1;
    refresh_cached_slot_thumb(sel);

    while (!g_app_should_quit) {
        ps3_check_sysutil();
        uint32_t pressed = 0;
        ps3_pad_poll_menu(&pressed);

        if (pressed & (PS3_ACT_CANCEL | PS3_ACT_MENU)) {
            g_quick_slot = sel;
            return;
        }
        if (pressed & PS3_ACT_UP) {
            sel = (sel + 9) % 10;
            refresh_cached_slot_thumb(sel);
        }
        if (pressed & PS3_ACT_DOWN) {
            sel = (sel + 1) % 10;
            refresh_cached_slot_thumb(sel);
        }
        if (pressed & PS3_ACT_CONFIRM) {
            if (ps3_state_exists(sel) && ps3_state_load(sel) == 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Loaded State Slot %d", sel);
                ps3_video_toast(msg, 120);
                g_quick_slot = sel;
                return;
            }
        }
        if (pressed & PS3_ACT_ALT1) {
            if (ps3_state_save(sel) == 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Saved State Slot %d", sel);
                ps3_video_toast(msg, 120);
                refresh_cached_slot_thumb(sel);
            }
        }
        if (pressed & PS3_ACT_ALT2) {
            if (ps3_state_exists(sel)) {
                ps3_state_delete(sel);
            }
        }

        ps3_video_render_game_to_fb(io.screen_w, io.screen_h);
        ps3_draw_rect(0, 0, PS3_SCREEN_W, PS3_SCREEN_H, 0xC8050A16u);

        int mw = 960;
        int mh = 560;
        int mx = (PS3_SCREEN_W - mw) / 2;
        int my = (PS3_SCREEN_H - mh) / 2;

        ps3_draw_panel(mx, my, mw, mh, 0xF6132648u, 0xF60A1326u, 0xFF4AD2FFu);
        ps3_draw_rect(mx + 2, my + 2, mw - 4, 4, 0xFFFF7A18u);
        ps3_draw_text_shadow(mx + 28, my + 16, 0xFFFFFFFFu, 2, "10-SLOT SAVE STATE MANAGER");
        ps3_draw_badge(mx + mw - 180, my + 18, g_rom_base, 0xFF1D4682u, 0xFF8AEBFFu);
        ps3_draw_hline(mx + 20, my + 52, mw - 40, 0xFF2B4F88u);

        int lx = mx + 24;
        int ly = my + 66;
        int lw = 500;
        for (int s = 0; s < 10; s++) {
            int ry = ly + s * 43;
            int has = ps3_state_exists(s);
            ps3_draw_card_row(lx, ry, lw, 38, s == sel);
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "State Slot %d", s);
            ps3_draw_text_shadow(lx + 18, ry + 8, (s == sel) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, lbl);
            ps3_draw_badge(lx + lw - 102, ry + 8,
                           has ? "SAVED" : "EMPTY",
                           has ? 0xFF167C52u : 0xFF1E2D4Au,
                           has ? 0xFFFFFFFFu : 0xFF8EA8D0u);
        }

        int rx = mx + 548;
        int ry = my + 66;
        int rw = mw - 572;
        ps3_draw_panel(rx, ry, rw, 426, 0xE80D1A32u, 0xE8081020u, 0xFF2D548Eu);
        ps3_draw_text_shadow(rx + 20, ry + 14, 0xFF7CE8FFu, 2, "STATE PREVIEW");

        ps3_draw_thumb_box(rx + 34, ry + 48, rw - 68, 240,
                           g_slot_thumb_valid ? g_slot_thumb_argb : NULL,
                           PS3_THUMB_W, PS3_THUMB_H,
                           ps3_state_exists(sel) ? "NO THUMBNAIL" : "SLOT EMPTY");

        int bx = rx + 24;
        int by = ry + 306;
        ps3_draw_btn_label(bx, by,      BTN_ICON_CROSS,    "Load Selected Slot", 0xFFE2EEFFu);
        ps3_draw_btn_label(bx, by + 30, BTN_ICON_SQUARE,   "Save Current Frame to Slot", 0xFFE2EEFFu);
        ps3_draw_btn_label(bx, by + 60, BTN_ICON_TRIANGLE, "Delete Selected State", 0xFFE2EEFFu);
        ps3_draw_btn_label(bx, by + 90, BTN_ICON_CIRCLE,   "Back to Quick Menu", 0xFFE2EEFFu);

        ps3_video_present();
    }
}

int ps3_ingame_menu_run(void)
{
    enum {
        QM_RESUME = 0,
        QM_SAVE_STATE,
        QM_LOAD_STATE,
        QM_STATE_MGR,
        QM_SCREENSHOT,
        QM_ASPECT,
        QM_FILTER,
        QM_SCANLINES,
        QM_CTRL_TYPE,
        QM_SWAP_BTNS,
        QM_VOLUME,
        QM_RESET,
        QM_QUIT_ROM,
        QM_EXIT_XMB,
        QM_COUNT
    };

    int sel = 0;
    ps3_audio_pause(1);

    for (int i = 0; i < 8; i++) {
        uint32_t dummy = 0;
        ps3_pad_poll_menu(&dummy);
    }

    g_slot_thumb_loaded_slot = -1;
    refresh_cached_slot_thumb(g_quick_slot);

    while (!g_app_should_quit) {
        ps3_check_sysutil();
        uint32_t pressed = 0;
        ps3_pad_poll_menu(&pressed);

        if (pressed & (PS3_ACT_CANCEL | PS3_ACT_MENU)) {
            ps3_config_save();
            ps3_audio_pause(0);
            return 0;
        }
        if (pressed & PS3_ACT_UP)   sel = (sel + QM_COUNT - 1) % QM_COUNT;
        if (pressed & PS3_ACT_DOWN) sel = (sel + 1) % QM_COUNT;

        int left  = (pressed & PS3_ACT_LEFT)  != 0;
        int right = (pressed & PS3_ACT_RIGHT) != 0;
        int enter = (pressed & (PS3_ACT_CONFIRM | PS3_ACT_START)) != 0;

        switch (sel) {
            case QM_RESUME:
                if (enter) {
                    ps3_config_save();
                    ps3_audio_pause(0);
                    return 0;
                }
                break;

            case QM_SAVE_STATE:
                if (left)  { g_quick_slot = (g_quick_slot + 9) % 10; refresh_cached_slot_thumb(g_quick_slot); }
                if (right) { g_quick_slot = (g_quick_slot + 1) % 10; refresh_cached_slot_thumb(g_quick_slot); }
                if (enter) {
                    char msg[64];
                    if (ps3_state_save(g_quick_slot) == 0) {
                        snprintf(msg, sizeof(msg), "Saved State Slot %d", g_quick_slot);
                    } else {
                        snprintf(msg, sizeof(msg), "Save Failed (Slot %d)", g_quick_slot);
                    }
                    ps3_video_toast(msg, 120);
                    ps3_config_save();
                    ps3_audio_pause(0);
                    return 0;
                }
                break;

            case QM_LOAD_STATE:
                if (left)  { g_quick_slot = (g_quick_slot + 9) % 10; refresh_cached_slot_thumb(g_quick_slot); }
                if (right) { g_quick_slot = (g_quick_slot + 1) % 10; refresh_cached_slot_thumb(g_quick_slot); }
                if (enter) {
                    char msg[64];
                    if (ps3_state_load(g_quick_slot) == 0) {
                        snprintf(msg, sizeof(msg), "Loaded State Slot %d", g_quick_slot);
                    } else {
                        snprintf(msg, sizeof(msg), "No State in Slot %d", g_quick_slot);
                    }
                    ps3_video_toast(msg, 120);
                    ps3_config_save();
                    ps3_audio_pause(0);
                    return 0;
                }
                break;

            case QM_STATE_MGR:
                if (enter) {
                    run_state_manager_modal();
                    refresh_cached_slot_thumb(g_quick_slot);
                }
                break;

            case QM_SCREENSHOT:
                if (enter) {
                    g_take_screenshot_pending = 1;
                    ps3_config_save();
                    ps3_audio_pause(0);
                    return 0;
                }
                break;

            case QM_ASPECT:
                if (left)  g_cfg.aspect = (g_cfg.aspect + PS3_ASPECT_COUNT - 1) % PS3_ASPECT_COUNT;
                if (right || enter) g_cfg.aspect = (g_cfg.aspect + 1) % PS3_ASPECT_COUNT;
                break;

            case QM_FILTER:
                if (left || right || enter) {
                    g_cfg.filter = (g_cfg.filter + 1) % PS3_FILTER_COUNT;
                    ps3_video_apply_filter();
                }
                break;

            case QM_SCANLINES:
                if (left || right || enter) g_cfg.scanlines = !g_cfg.scanlines;
                break;

            case QM_CTRL_TYPE:
                if (left)  g_cfg.six_button_pad = (g_cfg.six_button_pad + PS3_PAD_MODE_COUNT - 1) % PS3_PAD_MODE_COUNT;
                if (right || enter) g_cfg.six_button_pad = (g_cfg.six_button_pad + 1) % PS3_PAD_MODE_COUNT;
                if (g_cfg.six_button_pad == PS3_PAD_6BUTTON) {
                    g_six_button_mode = 1;
                } else if (g_cfg.six_button_pad == PS3_PAD_2BUTTON) {
                    g_six_button_mode = 0;
                } else {
                    g_six_button_mode = ps3_is_fighting_game(g_active_rom_path, ROM_size);
                }
                break;

            case QM_SWAP_BTNS:
                if (left || right || enter) g_cfg.swap_i_ii = !g_cfg.swap_i_ii;
                break;

            case QM_VOLUME:
                if (left  && g_cfg.audio_volume > 0)   g_cfg.audio_volume -= 5;
                if (right && g_cfg.audio_volume < 100) g_cfg.audio_volume += 5;
                break;

            case QM_RESET:
                if (enter) {
                    ResetPCE();
                    ps3_video_toast("Console Soft Reset", 120);
                    ps3_config_save();
                    ps3_audio_pause(0);
                    return 0;
                }
                break;

            case QM_QUIT_ROM:
                if (enter) {
                    ps3_bram_save(g_rom_base);
                    ps3_config_save();
                    g_emu_quit_to_browser = 1;
                    ps3_audio_pause(0);
                    return 1;
                }
                break;

            case QM_EXIT_XMB:
                if (enter) {
                    ps3_return_to_xmb();
                }
                break;
        }

        ps3_video_render_game_to_fb(io.screen_w, io.screen_h);
        ps3_draw_rect(0, 0, PS3_SCREEN_W, PS3_SCREEN_H, 0xB8050A16u);

        int mw = 980;
        int mh = 610;
        int mx = (PS3_SCREEN_W - mw) / 2;
        int my = (PS3_SCREEN_H - mh) / 2;

        ps3_draw_panel(mx, my, mw, mh, 0xF4122446u, 0xF4091224u, 0xFF4AD2FFu);
        ps3_draw_rect(mx + 2, my + 2, mw - 4, 4, 0xFFFF7A18u);

        ps3_draw_text_shadow(mx + 26, my + 16, 0xFFFFFFFFu, 2, "PCE-PS3  QUICK MENU");
        ps3_draw_badge(mx + mw - 240, my + 18, g_rom_base, 0xFF1D4682u, 0xFF8AEBFFu);
        ps3_draw_hline(mx + 20, my + 48, mw - 40, 0xFF2B4F88u);

        int lx = mx + 22;
        int ly = my + 52;
        int lw = 560;
        int rh = 34;
        int rg = 38;

        char slot_str[48];
        int has_st = ps3_state_exists(g_quick_slot);
        snprintf(slot_str, sizeof(slot_str), "Slot %d (%s)", g_quick_slot, has_st ? "SAVED" : "EMPTY");

        char vol_str[32];
        snprintf(vol_str, sizeof(vol_str), "%d%%", g_cfg.audio_volume);

        ps3_draw_card_row(lx, ly + 0 * rg, lw, rh, sel == QM_RESUME);
        ps3_draw_text_shadow(lx + 18, ly + 0 * rg + 7, (sel == QM_RESUME) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, "Resume Game");

        ps3_draw_selector_row(lx, ly + 1 * rg, lw, rh, "Quick Save State", slot_str, sel == QM_SAVE_STATE);
        ps3_draw_selector_row(lx, ly + 2 * rg, lw, rh, "Quick Load State", slot_str, sel == QM_LOAD_STATE);

        ps3_draw_card_row(lx, ly + 3 * rg, lw, rh, sel == QM_STATE_MGR);
        ps3_draw_text_shadow(lx + 18, ly + 3 * rg + 7, (sel == QM_STATE_MGR) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, "Open 10-Slot State Manager...");

        ps3_draw_card_row(lx, ly + 4 * rg, lw, rh, sel == QM_SCREENSHOT);
        ps3_draw_text_shadow(lx + 18, ly + 4 * rg + 7, (sel == QM_SCREENSHOT) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, "Take Screenshot (Save to HDD)");

        ps3_draw_selector_row(lx, ly + 5 * rg, lw, rh, "Aspect Ratio", aspect_label(g_cfg.aspect), sel == QM_ASPECT);
        ps3_draw_selector_row(lx, ly + 6 * rg, lw, rh, "Video Filter", filter_label(g_cfg.filter), sel == QM_FILTER);
        ps3_draw_switch_row(lx,   ly + 7 * rg, lw, rh, "CRT Scanlines", g_cfg.scanlines, sel == QM_SCANLINES);
        ps3_draw_selector_row(lx, ly + 8 * rg, lw, rh, "Controller Type",
                              pad_mode_label(g_cfg.six_button_pad),
                              sel == QM_CTRL_TYPE);
        ps3_draw_selector_row(lx, ly + 9 * rg, lw, rh, "Button Mapping",
                              g_cfg.swap_i_ii ? "Cross=I / Square=II" : "Circle=I / Cross=II",
                              sel == QM_SWAP_BTNS);
        ps3_draw_slider_row(lx,   ly + 10 * rg, lw, rh, "Master Volume", g_cfg.audio_volume, vol_str, sel == QM_VOLUME);

        ps3_draw_card_row(lx, ly + 11 * rg, lw, rh, sel == QM_RESET);
        ps3_draw_text_shadow(lx + 18, ly + 11 * rg + 7, (sel == QM_RESET) ? 0xFFFFB870u : 0xFFD0DEFAu, 2, "Soft Reset Console");

        ps3_draw_card_row(lx, ly + 12 * rg, lw, rh, sel == QM_QUIT_ROM);
        ps3_draw_text_shadow(lx + 18, ly + 12 * rg + 7, (sel == QM_QUIT_ROM) ? 0xFFFF9E3Du : 0xFFD0DEFAu, 2, "Return to Main Menu");

        ps3_draw_card_row(lx, ly + 13 * rg, lw, rh, sel == QM_EXIT_XMB);
        ps3_draw_text_shadow(lx + 18, ly + 13 * rg + 7, (sel == QM_EXIT_XMB) ? 0xFFFF5A6Au : 0xFFD0DEFAu, 2, "Exit PCE-PS3 (Return to XMB)");

        int rx = mx + 602;
        int ry = my + 54;
        int rw = mw - 624;
        ps3_draw_panel(rx, ry, rw, 524, 0xE80D1A32u, 0xE8081020u, 0xFF2D548Eu);

        char slot_hdr[48];
        snprintf(slot_hdr, sizeof(slot_hdr), "SLOT %d PREVIEW", g_quick_slot);
        ps3_draw_text_shadow(rx + 20, ry + 14, 0xFF7CE8FFu, 2, slot_hdr);
        ps3_draw_badge(rx + rw - 92, ry + 14,
                       has_st ? "SAVED" : "EMPTY",
                       has_st ? 0xFF167C52u : 0xFF1E2D4Au,
                       has_st ? 0xFFFFFFFFu : 0xFF8EA8D0u);

        ps3_draw_thumb_box(rx + 24, ry + 48, rw - 48, 224,
                           g_slot_thumb_valid ? g_slot_thumb_argb : NULL,
                           PS3_THUMB_W, PS3_THUMB_H,
                           has_st ? "SAVED STATE" : "EMPTY SLOT");

        ps3_draw_hline(rx + 18, ry + 288, rw - 36, 0xFF234274u);
        ps3_draw_text_shadow(rx + 20, ry + 300, 0xFFFF9E3Du, 1, "QUICK CONTROLS");
        ps3_draw_btn_label(rx + 20, ry + 328, BTN_ICON_DPAD,   "Navigate / Adjust", 0xFFDCE8FCu);
        ps3_draw_btn_label(rx + 20, ry + 360, BTN_ICON_CROSS,  "Confirm / Activate", 0xFFDCE8FCu);
        ps3_draw_btn_label(rx + 20, ry + 392, BTN_ICON_CIRCLE, "Resume Gameplay", 0xFFDCE8FCu);
        ps3_draw_btn_label(rx + 20, ry + 424, BTN_ICON_L2,     "+ R2  Toggle Quick Menu", 0xFFDCE8FCu);
        ps3_draw_btn_label(rx + 20, ry + 456, BTN_ICON_R3,     "In-Game Instant Screenshot", 0xFFDCE8FCu);

        ps3_video_present();
    }
    ps3_audio_pause(0);
    return 1;
}

static uint64_t g_last_frame_usec = 0;

static uint64_t now_frame_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

void ps3_frame_hook(void)
{
    interrupt_60hz();

    const uint64_t target = 16667ULL;
    uint64_t t = now_frame_usec();
    if (g_last_frame_usec != 0) {
        uint64_t elapsed = t - g_last_frame_usec;
        if (elapsed < target) {
            uint64_t wait_us = target - elapsed;
            if (wait_us > 500) {
                usleep((useconds_t)(wait_us - 200));
            }
        }
    }
    g_last_frame_usec = now_frame_usec();

    if (ps3_pad_ingame_combo_triggered()) {
        ps3_ingame_menu_run();
    }
}

typedef struct {
    char     name[256];
    char     full_path[512];
    int      is_dir;
    int      media_type;
    uint64_t size_bytes;
} file_entry_t;

#define MAX_ENTRIES 1024
static file_entry_t g_browser_entries[MAX_ENTRIES];
static int          g_browser_count = 0;

static file_entry_t g_lib_entries[MAX_ENTRIES];
static int          g_lib_count = 0;
static int          g_lib_filtered[MAX_ENTRIES];
static int          g_lib_filtered_count = 0;
static int          g_lib_filter_mode = 0;

static int detect_media_type(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return -1;
    if (strcasecmp(dot, ".pce") == 0) return PCE_MEDIA_HUCARD;
    if (strcasecmp(dot, ".sgx") == 0) return PCE_MEDIA_SGX;
    if (strcasecmp(dot, ".cue") == 0 ||
        strcasecmp(dot, ".iso") == 0 ||
        strcasecmp(dot, ".hcd") == 0) return PCE_MEDIA_CDROM;
    if (strcasecmp(dot, ".zip") == 0) return PCE_MEDIA_ZIP;
    return -1;
}

static const char *media_badge_str(int media_type)
{
    switch (media_type) {
        case PCE_MEDIA_HUCARD: return "HuCARD";
        case PCE_MEDIA_SGX:    return "SGX";
        case PCE_MEDIA_CDROM:  return "CD-ROM2";
        case PCE_MEDIA_ZIP:    return "ZIP";
        default:               return "ROM";
    }
}

static uint32_t media_badge_color(int media_type)
{
    switch (media_type) {
        case PCE_MEDIA_HUCARD: return 0xFFD95B12u;
        case PCE_MEDIA_SGX:    return 0xFF1A78C2u;
        case PCE_MEDIA_CDROM:  return 0xFF8838B8u;
        case PCE_MEDIA_ZIP:    return 0xFF287E5Cu;
        default:               return 0xFF2B4F88u;
    }
}

static int cmp_entries(const void *a, const void *b)
{
    const file_entry_t *ea = (const file_entry_t *)a;
    const file_entry_t *eb = (const file_entry_t *)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcasecmp(ea->name, eb->name);
}

static void scan_directory_browser(const char *dir_path)
{
    g_browser_count = 0;

    if (strcmp(dir_path, "/") != 0) {
        strcpy(g_browser_entries[g_browser_count].name, ".. (Parent Directory)");
        strcpy(g_browser_entries[g_browser_count].full_path, "..");
        g_browser_entries[g_browser_count].is_dir = 1;
        g_browser_entries[g_browser_count].media_type = -1;
        g_browser_entries[g_browser_count].size_bytes = 0;
        g_browser_count++;
    }

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_browser_count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;

        char full[512];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", de->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
        }

        struct stat st;
        int is_dir = 0;
        uint64_t sz = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            sz = (uint64_t)st.st_size;
        } else if (de->d_type == DT_DIR) {
            is_dir = 1;
        }

        int mtype = is_dir ? -1 : detect_media_type(de->d_name);
        if (!is_dir && mtype < 0) continue;

        snprintf(g_browser_entries[g_browser_count].name, sizeof(g_browser_entries[g_browser_count].name), "%s", de->d_name);
        snprintf(g_browser_entries[g_browser_count].full_path, sizeof(g_browser_entries[g_browser_count].full_path), "%s", full);
        g_browser_entries[g_browser_count].is_dir = is_dir;
        g_browser_entries[g_browser_count].media_type = mtype;
        g_browser_entries[g_browser_count].size_bytes = sz;
        g_browser_count++;
    }
    closedir(d);

    int start = (g_browser_count > 0 && strcmp(g_browser_entries[0].full_path, "..") == 0) ? 1 : 0;
    if (g_browser_count - start > 1) {
        qsort(&g_browser_entries[start], (size_t)(g_browser_count - start), sizeof(file_entry_t), cmp_entries);
    }
}

static void scan_library_folder_recursive(const char *dir_path, int depth)
{
    if (depth > 2 || g_lib_count >= MAX_ENTRIES) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_lib_count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_library_folder_recursive(full, depth + 1);
            continue;
        }

        int mtype = detect_media_type(de->d_name);
        if (mtype < 0) continue;

        int dup = 0;
        for (int i = 0; i < g_lib_count; i++) {
            if (strcmp(g_lib_entries[i].full_path, full) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        snprintf(g_lib_entries[g_lib_count].name, sizeof(g_lib_entries[g_lib_count].name), "%s", de->d_name);
        snprintf(g_lib_entries[g_lib_count].full_path, sizeof(g_lib_entries[g_lib_count].full_path), "%s", full);
        g_lib_entries[g_lib_count].is_dir = 0;
        g_lib_entries[g_lib_count].media_type = mtype;
        g_lib_entries[g_lib_count].size_bytes = (uint64_t)st.st_size;
        g_lib_count++;
    }
    closedir(d);
}

static void rebuild_library_filter(void)
{
    g_lib_filtered_count = 0;
    for (int i = 0; i < g_lib_count; i++) {
        int m = g_lib_entries[i].media_type;
        if (g_lib_filter_mode == 1 && m != PCE_MEDIA_HUCARD && m != PCE_MEDIA_ZIP) continue;
        if (g_lib_filter_mode == 2 && m != PCE_MEDIA_SGX) continue;
        if (g_lib_filter_mode == 3 && m != PCE_MEDIA_CDROM) continue;
        g_lib_filtered[g_lib_filtered_count++] = i;
    }
}

static void scan_usb_root_files(const char *root_path)
{
    if (g_lib_count >= MAX_ENTRIES) return;
    DIR *d = opendir(root_path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_lib_count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        int mtype = detect_media_type(de->d_name);
        if (mtype < 0) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", root_path, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        int dup = 0;
        for (int i = 0; i < g_lib_count; i++) {
            if (strcmp(g_lib_entries[i].full_path, full) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        snprintf(g_lib_entries[g_lib_count].name, sizeof(g_lib_entries[g_lib_count].name), "%s", de->d_name);
        snprintf(g_lib_entries[g_lib_count].full_path, sizeof(g_lib_entries[g_lib_count].full_path), "%s", full);
        g_lib_entries[g_lib_count].is_dir = 0;
        g_lib_entries[g_lib_count].media_type = mtype;
        g_lib_entries[g_lib_count].size_bytes = (uint64_t)st.st_size;
        g_lib_count++;
    }
    closedir(d);
}

static void rescan_all_library_sources(void)
{
    g_lib_count = 0;
    scan_library_folder_recursive(PS3_ROMS_DIR, 0);
    scan_library_folder_recursive("/dev_hdd0/ROMS/pce", 0);
    scan_library_folder_recursive("/dev_hdd0/roms/pce", 0);
    scan_library_folder_recursive(PS3_PKG_USRDIR "/roms", 0);

    for (int u = 0; u < 4; u++) {
        char uroot[64];
        snprintf(uroot, sizeof(uroot), "/dev_usb00%d", u);
        DIR *dcheck = opendir(uroot);
        if (!dcheck) continue;
        closedir(dcheck);

        scan_usb_root_files(uroot);
        char upath[128];
        snprintf(upath, sizeof(upath), PS3_USB_ROMS_FMT, u);
        scan_library_folder_recursive(upath, 0);
        snprintf(upath, sizeof(upath), PS3_USB_ROOT_FMT, u);
        scan_library_folder_recursive(upath, 0);
        snprintf(upath, sizeof(upath), PS3_USB_ALT_FMT, u);
        scan_library_folder_recursive(upath, 0);
        snprintf(upath, sizeof(upath), "/dev_usb00%d/ROMS", u);
        scan_library_folder_recursive(upath, 0);
        snprintf(upath, sizeof(upath), "/dev_usb00%d/roms", u);
        scan_library_folder_recursive(upath, 0);
    }

    if (g_lib_count > 1) {
        qsort(g_lib_entries, (size_t)g_lib_count, sizeof(file_entry_t), cmp_entries);
    }
    rebuild_library_filter();
}

static int copy_file_binary(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    unsigned char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int auto_import_bios_from_usb(char *out_status, size_t cap)
{
    static const char *bios_names[] = {
        "syscard3.pce", "SYSCARD3.PCE",
        "syscard2.pce", "SYSCARD2.PCE",
        "syscard1.pce", "SYSCARD1.PCE",
        "syscard.pce",  "SYSCARD.PCE",
        "bios.pce",     "BIOS.PCE",
        "games_express.pce",
        NULL
    };
    static const char *sub_dirs[] = {
        "", "/bios", "/BIOS", "/pce", "/PCE", "/roms/pce", "/pce4ps3/bios", NULL
    };

    int imported = 0;
    for (int u = 0; u < 8; u++) {
        char uroot[64];
        snprintf(uroot, sizeof(uroot), "/dev_usb00%d", u);
        DIR *dcheck = opendir(uroot);
        if (!dcheck) continue;
        closedir(dcheck);

        for (int s = 0; sub_dirs[s]; s++) {
            for (int b = 0; bios_names[b]; b++) {
                char src[512];
                snprintf(src, sizeof(src), "/dev_usb00%d%s/%s", u, sub_dirs[s], bios_names[b]);
                struct stat st;
                if (stat(src, &st) == 0 && st.st_size >= 8192) {
                    char dst[512];
                    const char *canon = (strncasecmp(bios_names[b], "syscard2", 8) == 0) ? "syscard2.pce"
                                      : (strncasecmp(bios_names[b], "syscard1", 8) == 0) ? "syscard1.pce"
                                      : (strncasecmp(bios_names[b], "bios", 4) == 0)     ? "bios.pce"
                                      : "syscard3.pce";
                    snprintf(dst, sizeof(dst), "%s/%s", PS3_BIOS_DIR, canon);
                    if (copy_file_binary(src, dst) == 0) {
                        imported++;
                    }
                }
            }
        }
    }

    if (imported > 0) {
        snprintf(out_status, cap, "Imported %d System Card BIOS file(s) to %s!", imported, PS3_BIOS_DIR);
    } else {
        snprintf(out_status, cap, "No BIOS files found on USB (/dev_usb000..007). Place syscard3.pce on USB.");
    }
    return imported;
}

int ps3_validate_pce_file(const char *path)
{
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fclose(f);
    if (fsize < 16384 || fsize > (8 * 1024 * 1024)) {
        return 0;
    }
    return 1;
}

static void launch_selected_game(const char *selected_path)
{
    if (!selected_path || !selected_path[0]) return;

    snprintf(g_active_rom_path, sizeof(g_active_rom_path), "%s", selected_path);
    extract_rom_basename(selected_path, g_rom_base, sizeof(g_rom_base));

    ps3_draw_boot_splash("LOADING ROM / CD-ROM2 MEDIA...", 75);

    char actual_rom_path[512];
    snprintf(actual_rom_path, sizeof(actual_rom_path), "%s", selected_path);

    const char *dot = strrchr(selected_path, '.');
    int is_cue = (dot && strcasecmp(dot, ".cue") == 0);
    int is_iso = (dot && strcasecmp(dot, ".iso") == 0);
    int is_hcd = (dot && strcasecmp(dot, ".hcd") == 0);
    int is_zip = (dot && strcasecmp(dot, ".zip") == 0);

    if (is_zip) {
        if (ps3_zip_extract_pce(selected_path, actual_rom_path, sizeof(actual_rom_path)) != 0) {
            ps3_log("Failed to extract ZIP: %s\n", selected_path);
            ps3_show_error_dialog("INVALID ZIP ARCHIVE",
                                  "No PC Engine ROM (.pce / .sgx) found.",
                                  "Please check that the archive contains a valid PC Engine ROM.");
            return;
        }
    }

    if (!is_cue && !is_iso && !is_hcd) {
        if (!ps3_validate_pce_file(actual_rom_path)) {
            ps3_show_error_dialog("INVALID ROM FILE",
                                  "The selected file is not a valid PC Engine ROM.",
                                  "File size or format does not match a PC Engine game.");
            ps3_zip_cleanup_temp();
            return;
        }
    }

    ps3_cue_unload();
    option.want_supergraphx_emulation = (dot && strcasecmp(dot, ".sgx") == 0) ? 1 : 0;
    option.want_arcade_card_emulation = 1;

    char bram_file[512];
    snprintf(bram_file, sizeof(bram_file), "%s/%s.brm", PS3_BRAM_DIR, g_rom_base);

    char syscard_path[512] = {0};
    if (is_cue || is_iso || is_hcd) {
        if (!ps3_find_syscard(syscard_path, sizeof(syscard_path))) {
            ps3_log("CD-ROM2 game requested but syscard3.pce not found!\n");
            ps3_show_error_dialog("MISSING SYSCARD3.PCE BIOS",
                                  "CD-ROM2 titles require the syscard3.pce System Card BIOS.",
                                  "Import it from the BIOS tab or place it on a USB flash drive.");
            ps3_zip_cleanup_temp();
            return;
        }
        snprintf(cdsystem_path, sizeof(cdsystem_path), "%s", syscard_path);
    }

    int init_rc = -1;
    if (is_cue) {
        if (ps3_cue_load(actual_rom_path) == 0) {
            CD_emulation = 2;
            snprintf(ISO_filename, sizeof(ISO_filename), "%s", actual_rom_path);
            init_rc = InitPCE(syscard_path, bram_file);
        }
    } else if (is_iso) {
        CD_emulation = 2;
        snprintf(ISO_filename, sizeof(ISO_filename), "%s", actual_rom_path);
        init_rc = InitPCE(syscard_path, bram_file);
    } else if (is_hcd) {
        CD_emulation = 5;
        snprintf(ISO_filename, sizeof(ISO_filename), "%s", actual_rom_path);
        init_rc = InitPCE(syscard_path, bram_file);
    } else {
        CD_emulation = 0;
        ISO_filename[0] = '\0';
        init_rc = InitPCE(actual_rom_path, bram_file);
    }

    if (init_rc == 0) {
        if (ROM_size > 0x80) {
            write_memory_function = write_memory_sf2;
        }
        if (g_cfg.six_button_pad == PS3_PAD_6BUTTON) {
            g_six_button_mode = 1;
        } else if (g_cfg.six_button_pad == PS3_PAD_2BUTTON) {
            g_six_button_mode = 0;
        } else {
            g_six_button_mode = ps3_is_fighting_game(actual_rom_path, ROM_size);
        }
        osd_init_machine();
        g_game_loaded         = 1;
        g_emu_quit_to_browser = 0;
        g_slot_thumb_loaded_slot = -1;
        ps3_bram_load(g_rom_base);
        ps3_audio_pause(0);
        char launch_msg[256];
        snprintf(launch_msg, sizeof(launch_msg), "%s", g_rom_base);
        ps3_video_toast(launch_msg, 120);
        ensure_paddata_init();
        for (int p = 0; p < MAX_PADS; p++) reset_paddata_entry(p);
        RunPCE();
        ps3_audio_pause(1);
        ps3_bram_save(g_rom_base);
        TrashPCE(bram_file);
    } else {
        ps3_log("InitPCE failed for %s\n", actual_rom_path);
        ps3_show_error_dialog("EMULATION INIT ERROR",
                              "Failed to initialize PC Engine emulation core.",
                              "The file may be corrupted or in an incompatible format.");
    }

    ps3_cue_unload();
    ps3_zip_cleanup_temp();
}

static void run_main_frontend(void)
{
    enum {
        TAB_LIBRARY = 0,
        TAB_BROWSER,
        TAB_STATES,
        TAB_SETTINGS,
        TAB_BIOS,
        TAB_CREDITS,
        TAB_COUNT
    };
    static const char *tab_names[TAB_COUNT] = {
        "LIBRARY",
        "FILE BROWSER",
        "SAVE STATES",
        "VIDEO & AUDIO",
        "BIOS & CD-ROM2",
        "CREDITS & EXIT"
    };
    static const char *filter_names[4] = {
        "ALL MEDIA",
        "HuCARD (.PCE)",
        "SUPERGRAFX (.SGX)",
        "CD-ROM2 (.CUE/.ISO)"
    };
    static const char *bookmark_roots[] = {
        PS3_ROMS_DIR,
        "/dev_hdd0",
        "/dev_usb000",
        "/dev_usb001",
        "/dev_usb002"
    };

    int active_tab   = TAB_LIBRARY;
    int lib_sel      = 0;
    int lib_scroll   = 0;
    int brw_sel      = 0;
    int brw_scroll   = 0;
    int st_sel       = 0;
    int cfg_sel      = 0;
    int bookmark_idx = 0;
    char bios_msg[160] = "Press TRIANGLE to auto-import System Card BIOS files from USB.";

    char cur_dir[512];
    snprintf(cur_dir, sizeof(cur_dir), "%s", g_cfg.last_rom_dir);

    scan_directory_browser(cur_dir);
    if (g_browser_count == 0) {
        strcpy(cur_dir, "/dev_hdd0");
        scan_directory_browser(cur_dir);
    }
    rescan_all_library_sources();

    char cached_syscard[512] = {0};
    int cached_has_bios = ps3_find_syscard(cached_syscard, sizeof(cached_syscard));
    int cached_state_exists[10];
    for (int s = 0; s < 10; s++) {
        cached_state_exists[s] = ps3_state_exists(s);
    }

    const int visible_rows = 10;

    while (!g_app_should_quit) {
        ps3_check_sysutil();
        uint32_t pressed = 0;
        ps3_pad_poll_menu(&pressed);
        if (g_app_should_quit) break;

        if (pressed & PS3_ACT_L1) {
            active_tab = (active_tab + TAB_COUNT - 1) % TAB_COUNT;
            if (active_tab == TAB_STATES) {
                g_slot_thumb_loaded_slot = -1;
                refresh_cached_slot_thumb(st_sel);
                for (int s = 0; s < 10; s++) cached_state_exists[s] = ps3_state_exists(s);
            }
            if (active_tab == TAB_BIOS || active_tab == TAB_LIBRARY) {
                cached_has_bios = ps3_find_syscard(cached_syscard, sizeof(cached_syscard));
            }
        }
        if (pressed & PS3_ACT_R1) {
            active_tab = (active_tab + 1) % TAB_COUNT;
            if (active_tab == TAB_STATES) {
                g_slot_thumb_loaded_slot = -1;
                refresh_cached_slot_thumb(st_sel);
                for (int s = 0; s < 10; s++) cached_state_exists[s] = ps3_state_exists(s);
            }
            if (active_tab == TAB_BIOS || active_tab == TAB_LIBRARY) {
                cached_has_bios = ps3_find_syscard(cached_syscard, sizeof(cached_syscard));
            }
        }

        if (active_tab == TAB_LIBRARY) {
            if (g_lib_filtered_count > 0) {
                if (pressed & PS3_ACT_UP)   lib_sel = (lib_sel + g_lib_filtered_count - 1) % g_lib_filtered_count;
                if (pressed & PS3_ACT_DOWN) lib_sel = (lib_sel + 1) % g_lib_filtered_count;
                if (pressed & PS3_ACT_LEFT) {
                    lib_sel -= visible_rows;
                    if (lib_sel < 0) lib_sel = 0;
                }
                if (pressed & PS3_ACT_RIGHT) {
                    lib_sel += visible_rows;
                    if (lib_sel >= g_lib_filtered_count) lib_sel = g_lib_filtered_count - 1;
                }
                if (lib_sel < lib_scroll) lib_scroll = lib_sel;
                if (lib_sel >= lib_scroll + visible_rows) lib_scroll = lib_sel - visible_rows + 1;
            }
            if (pressed & PS3_ACT_ALT1) {
                g_lib_filter_mode = (g_lib_filter_mode + 1) % 4;
                rebuild_library_filter();
                lib_sel = 0;
                lib_scroll = 0;
            }
            if (pressed & PS3_ACT_ALT2) {
                ps3_draw_boot_splash("RESCANNING HDD & USB GAME LIBRARY...", 60);
                rescan_all_library_sources();
                cached_has_bios = ps3_find_syscard(cached_syscard, sizeof(cached_syscard));
                lib_sel = 0;
                lib_scroll = 0;
            }
            if ((pressed & (PS3_ACT_CONFIRM | PS3_ACT_START)) && g_lib_filtered_count > 0) {
                const file_entry_t *e = &g_lib_entries[g_lib_filtered[lib_sel]];
                launch_selected_game(e->full_path);
            }
        } else if (active_tab == TAB_BROWSER) {
            if (g_browser_count > 0) {
                if (pressed & PS3_ACT_UP)   brw_sel = (brw_sel + g_browser_count - 1) % g_browser_count;
                if (pressed & PS3_ACT_DOWN) brw_sel = (brw_sel + 1) % g_browser_count;
                if (pressed & PS3_ACT_LEFT) {
                    brw_sel -= visible_rows;
                    if (brw_sel < 0) brw_sel = 0;
                }
                if (pressed & PS3_ACT_RIGHT) {
                    brw_sel += visible_rows;
                    if (brw_sel >= g_browser_count) brw_sel = g_browser_count - 1;
                }
                if (brw_sel < brw_scroll) brw_scroll = brw_sel;
                if (brw_sel >= brw_scroll + visible_rows) brw_scroll = brw_sel - visible_rows + 1;
            }
            if (pressed & PS3_ACT_SELECT) {
                bookmark_idx = (bookmark_idx + 1) % (int)(sizeof(bookmark_roots) / sizeof(bookmark_roots[0]));
                snprintf(cur_dir, sizeof(cur_dir), "%s", bookmark_roots[bookmark_idx]);
                scan_directory_browser(cur_dir);
                brw_sel = 0;
                brw_scroll = 0;
            }
            if (pressed & PS3_ACT_ALT2) {
                char *slash = strrchr(cur_dir, '/');
                if (slash && slash != cur_dir) *slash = '\0';
                else strcpy(cur_dir, "/");
                scan_directory_browser(cur_dir);
                brw_sel = 0;
                brw_scroll = 0;
            }
            if ((pressed & (PS3_ACT_CONFIRM | PS3_ACT_START)) && g_browser_count > 0) {
                file_entry_t *e = &g_browser_entries[brw_sel];
                if (e->is_dir) {
                    if (strcmp(e->full_path, "..") == 0) {
                        char *slash = strrchr(cur_dir, '/');
                        if (slash && slash != cur_dir) *slash = '\0';
                        else strcpy(cur_dir, "/");
                    } else {
                        snprintf(cur_dir, sizeof(cur_dir), "%s", e->full_path);
                    }
                    scan_directory_browser(cur_dir);
                    brw_sel = 0;
                    brw_scroll = 0;
                } else {
                    snprintf(g_cfg.last_rom_dir, sizeof(g_cfg.last_rom_dir), "%s", cur_dir);
                    ps3_config_save();
                    launch_selected_game(e->full_path);
                }
            }
        } else if (active_tab == TAB_STATES) {
            if (pressed & PS3_ACT_UP) {
                st_sel = (st_sel + 9) % 10;
                refresh_cached_slot_thumb(st_sel);
            }
            if (pressed & PS3_ACT_DOWN) {
                st_sel = (st_sel + 1) % 10;
                refresh_cached_slot_thumb(st_sel);
            }
            if (pressed & PS3_ACT_ALT2) {
                if (cached_state_exists[st_sel]) {
                    ps3_state_delete(st_sel);
                    cached_state_exists[st_sel] = 0;
                    refresh_cached_slot_thumb(st_sel);
                }
            }
            if ((pressed & PS3_ACT_CONFIRM) && g_active_rom_path[0] && cached_state_exists[st_sel]) {
                g_quick_slot = st_sel;
                launch_selected_game(g_active_rom_path);
            }
        } else if (active_tab == TAB_SETTINGS) {
            const int cfg_rows = 10;
            if (pressed & PS3_ACT_UP)   cfg_sel = (cfg_sel + cfg_rows - 1) % cfg_rows;
            if (pressed & PS3_ACT_DOWN) cfg_sel = (cfg_sel + 1) % cfg_rows;
            int left  = (pressed & PS3_ACT_LEFT)  != 0;
            int right = (pressed & PS3_ACT_RIGHT) != 0;
            int enter = (pressed & PS3_ACT_CONFIRM) != 0;

            switch (cfg_sel) {
                case 0:
                    if (left)  g_cfg.aspect = (g_cfg.aspect + PS3_ASPECT_COUNT - 1) % PS3_ASPECT_COUNT;
                    if (right || enter) g_cfg.aspect = (g_cfg.aspect + 1) % PS3_ASPECT_COUNT;
                    break;
                case 1:
                    if (left || right || enter) {
                        g_cfg.filter = (g_cfg.filter + 1) % PS3_FILTER_COUNT;
                        ps3_video_apply_filter();
                    }
                    break;
                case 2:
                    if (left || right || enter) g_cfg.scanlines = !g_cfg.scanlines;
                    break;
                case 3:
                    if (left  && g_cfg.audio_volume > 0)   g_cfg.audio_volume -= 5;
                    if (right && g_cfg.audio_volume < 100) g_cfg.audio_volume += 5;
                    break;
                case 4:
                    if (left)  g_cfg.six_button_pad = (g_cfg.six_button_pad + PS3_PAD_MODE_COUNT - 1) % PS3_PAD_MODE_COUNT;
                    if (right || enter) g_cfg.six_button_pad = (g_cfg.six_button_pad + 1) % PS3_PAD_MODE_COUNT;
                    break;
                case 5:
                    if (left || right || enter) g_cfg.swap_i_ii = !g_cfg.swap_i_ii;
                    break;
                case 6:
                    if (left  && g_cfg.turbo_rate > 1) g_cfg.turbo_rate--;
                    if (right && g_cfg.turbo_rate < 4) g_cfg.turbo_rate++;
                    if (enter) g_cfg.turbo_rate = (g_cfg.turbo_rate % 4) + 1;
                    break;
                case 7:
                    if (left || right || enter) g_cfg.show_fps = !g_cfg.show_fps;
                    break;
                case 8:
                    if (enter) ps3_config_save();
                    break;
                case 9:
                    if (enter) ps3_return_to_xmb();
                    break;
            }
        } else if (active_tab == TAB_BIOS) {
            if (pressed & PS3_ACT_ALT2) {
                auto_import_bios_from_usb(bios_msg, sizeof(bios_msg));
                cached_has_bios = ps3_find_syscard(cached_syscard, sizeof(cached_syscard));
            }
        } else if (active_tab == TAB_CREDITS) {
            if (pressed & (PS3_ACT_CONFIRM | PS3_ACT_START)) {
                ps3_return_to_xmb();
            }
        }

        char right_hdr[64];
        snprintf(right_hdr, sizeof(right_hdr), "%d GAMES FOUND", g_lib_count);
        ps3_draw_pce_background(right_hdr);
        ps3_draw_tab_bar(74, tab_names, TAB_COUNT, active_tab);

        int content_y = 118;
        int content_h = 516;

        if (active_tab == TAB_LIBRARY) {
            int lx = 32;
            int lw = 760;
            int rx = 808;
            int rw = PS3_SCREEN_W - rx - 32;

            ps3_draw_panel(lx, content_y, lw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(lx + 20, content_y + 12, 0xFF7CE8FFu, 2, "PC ENGINE & CD-ROM2 LIBRARY");
            ps3_draw_badge(lx + lw - 210, content_y + 14, filter_names[g_lib_filter_mode], 0xFF1F4B8Au, 0xFFFFFFFFu);
            ps3_draw_hline(lx + 16, content_y + 44, lw - 32, 0xFF244374u);

            if (g_lib_filtered_count == 0) {
                const char *msg1 = "No games found for this filter";
                const char *msg2 = "Press SQUARE to change filter (All/HuCARD/SGX/CD)";
                const char *msg3 = "Press TRIANGLE to scan HDD and USB drives";
                const char *msg4 = "Or press R1 to use the File Browser";
                int w1 = ps3_text_width(2, msg1);
                int w2 = ps3_text_width(1, msg2);
                int w3 = ps3_text_width(1, msg3);
                int w4 = ps3_text_width(1, msg4);
                ps3_draw_text_shadow(lx + (lw - w1) / 2, content_y + 140, 0xFFFFFFFFu, 2, msg1);
                ps3_draw_text_shadow(lx + (lw - w2) / 2, content_y + 190, 0xFF7CE8FFu, 1, msg2);
                ps3_draw_text_shadow(lx + (lw - w3) / 2, content_y + 222, 0xFFD0DEFAu, 1, msg3);
                ps3_draw_text_shadow(lx + (lw - w4) / 2, content_y + 254, 0xFF8AA8D8u, 1, msg4);

                ps3_draw_panel(rx, content_y, rw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
                ps3_draw_text_shadow(rx + 20, content_y + 12, 0xFF7CE8FFu, 2, "WHERE TO PLACE ROMS");
                ps3_draw_hline(rx + 16, content_y + 44, rw - 32, 0xFF244374u);

                ps3_draw_text_shadow(rx + 20, content_y + 60, 0xFFFFCC00u, 1, "USB DRIVE (FAT32):");
                ps3_draw_text(rx + 20, content_y + 86, 0xFFFFFFFFu, 1, "dev_usb00x/roms/pce/");
                ps3_draw_text(rx + 20, content_y + 110, 0xFFFFFFFFu, 1, "dev_usb00x/pce/  or  /ROMS/");
                ps3_draw_text(rx + 20, content_y + 134, 0xFFD0DEFAu, 1, "or directly in USB root");

                ps3_draw_text_shadow(rx + 20, content_y + 174, 0xFFFFCC00u, 1, "PS3 INTERNAL HARD DISK:");
                ps3_draw_text(rx + 20, content_y + 200, 0xFFFFFFFFu, 1, "/dev_hdd0/pce4ps3/roms/");
                ps3_draw_text(rx + 20, content_y + 224, 0xFFFFFFFFu, 1, "/dev_hdd0/ROMS/pce/");
                ps3_draw_text(rx + 20, content_y + 248, 0xFFFFFFFFu, 1, "/dev_hdd0/roms/pce/");

                ps3_draw_text_shadow(rx + 20, content_y + 288, 0xFFFFCC00u, 1, "SUPPORTED FORMATS:");
                ps3_draw_text(rx + 20, content_y + 314, 0xFF7CE8FFu, 1, ".pce (HuCARD, also inside .zip)");
                ps3_draw_text(rx + 20, content_y + 338, 0xFF7CE8FFu, 1, ".sgx (SuperGrafx)");
                ps3_draw_text(rx + 20, content_y + 362, 0xFF7CE8FFu, 1, ".cue, .iso, .hcd (CD-ROM2)");

                ps3_draw_text_shadow(rx + 20, content_y + 402, 0xFF8AA8D8u, 1, "For CD-ROM2 place 'syscard3.pce' in:");
                ps3_draw_text(rx + 20, content_y + 426, 0xFFD0DEFAu, 1, "/dev_hdd0/pce4ps3/bios/");
                ps3_draw_text(rx + 20, content_y + 450, 0xFF8AA8D8u, 1, "(HuCard .pce games do not require BIOS)");
            } else {
                for (int row = 0; row < visible_rows; row++) {
                    int idx = lib_scroll + row;
                    if (idx >= g_lib_filtered_count) break;
                    const file_entry_t *e = &g_lib_entries[g_lib_filtered[idx]];
                    int ry = content_y + 54 + row * 45;
                    ps3_draw_card_row(lx + 14, ry, lw - 28, 41, idx == lib_sel);

                    int bw = ps3_draw_badge(lx + 28, ry + 9, media_badge_str(e->media_type),
                                            media_badge_color(e->media_type), 0xFFFFFFFFu);
                    ps3_draw_text_clipped(lx + 38 + bw, ry + 9, lw - bw - 160,
                                          (idx == lib_sel) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, e->name);

                    char sz_buf[32];
                    if (e->size_bytes >= 1048576ULL) {
                        snprintf(sz_buf, sizeof(sz_buf), "%.1f MB", (double)e->size_bytes / 1048576.0);
                    } else {
                        snprintf(sz_buf, sizeof(sz_buf), "%llu KB", (unsigned long long)(e->size_bytes / 1024ULL));
                    }
                    int sw = ps3_text_width(1, sz_buf);
                    ps3_draw_text(lx + lw - sw - 28, ry + 12, 0xFF8AA8D8u, 1, sz_buf);
                }

                const file_entry_t *sel_e = &g_lib_entries[g_lib_filtered[lib_sel]];
                ps3_draw_media_card(rx, content_y, rw, 310, sel_e->name, sel_e->media_type, media_badge_str(sel_e->media_type));

                ps3_draw_panel(rx, content_y + 322, rw, content_h - 322, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
                ps3_draw_text_shadow(rx + 18, content_y + 334, 0xFFFF9E3Du, 1, "MEDIA DETAILS");
                ps3_draw_text_clipped(rx + 18, content_y + 362, rw - 36, 0xFFDCE8FCu, 1, sel_e->full_path);

                char dev_line[96];
                snprintf(dev_line, sizeof(dev_line), "Storage: %s   |   Item %d of %d",
                         (strncmp(sel_e->full_path, "/dev_usb", 8) == 0) ? "USB Drive" : "Internal HDD",
                         lib_sel + 1, g_lib_filtered_count);
                ps3_draw_text(rx + 18, content_y + 390, 0xFF8CE8FFu, 1, dev_line);

                ps3_draw_text(rx + 18, content_y + 418,
                              cached_has_bios ? 0xFF5CE8A0u : 0xFFFFB066u, 1,
                              cached_has_bios ? "CD-ROM2 System Card: READY" : "CD-ROM2 System Card: MISSING (See BIOS Tab)");
            }

            const int btns[] = { BTN_ICON_CROSS, BTN_ICON_SQUARE, BTN_ICON_TRIANGLE, BTN_ICON_L1 };
            const char *lbls[] = { "Launch Game", "Filter Media Type", "Rescan Media", "Switch Tabs" };
            ps3_draw_footer_buttons(btns, lbls, 4);

        } else if (active_tab == TAB_BROWSER) {
            int lx = 32;
            int lw = 780;
            int rx = 828;
            int rw = PS3_SCREEN_W - rx - 32;

            ps3_draw_panel(lx, content_y, lw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_clipped(lx + 20, content_y + 12, lw - 40, 0xFF7CE8FFu, 2, cur_dir);
            ps3_draw_hline(lx + 16, content_y + 44, lw - 32, 0xFF244374u);

            for (int row = 0; row < visible_rows; row++) {
                int idx = brw_scroll + row;
                if (idx >= g_browser_count) break;
                const file_entry_t *e = &g_browser_entries[idx];
                int ry = content_y + 54 + row * 45;
                ps3_draw_card_row(lx + 14, ry, lw - 28, 41, idx == brw_sel);

                if (e->is_dir) {
                    int bw = ps3_draw_badge(lx + 28, ry + 9, "DIR", 0xFF28589Cu, 0xFFFFFFFFu);
                    ps3_draw_text_clipped(lx + 38 + bw, ry + 9, lw - bw - 80,
                                          (idx == brw_sel) ? 0xFFFFD180u : 0xFFB8D4FFu, 2, e->name);
                } else {
                    int bw = ps3_draw_badge(lx + 28, ry + 9, media_badge_str(e->media_type),
                                            media_badge_color(e->media_type), 0xFFFFFFFFu);
                    ps3_draw_text_clipped(lx + 38 + bw, ry + 9, lw - bw - 150,
                                          (idx == brw_sel) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, e->name);
                }
            }

            ps3_draw_panel(rx, content_y, rw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(rx + 20, content_y + 14, 0xFFFF9E3Du, 2, "QUICK ROOT BOOKMARKS");
            ps3_draw_text(rx + 20, content_y + 46, 0xFF9AC2FAu, 1, "Press SELECT to cycle storage roots:");
            for (int b = 0; b < (int)(sizeof(bookmark_roots) / sizeof(bookmark_roots[0])); b++) {
                int by = content_y + 78 + b * 38;
                ps3_draw_card_row(rx + 16, by, rw - 32, 34, b == bookmark_idx);
                ps3_draw_text(rx + 30, by + 8, (b == bookmark_idx) ? 0xFFFFFFFFu : 0xFFC8D8F2u, 1, bookmark_roots[b]);
            }

            ps3_draw_hline(rx + 18, content_y + 284, rw - 36, 0xFF244374u);
            ps3_draw_text_shadow(rx + 20, content_y + 300, 0xFF7CE8FFu, 2, "SUPPORTED FORMATS");
            ps3_draw_text(rx + 20, content_y + 336, 0xFFDCE8FCu, 1, "- .pce : PC Engine / TurboGrafx-16 HuCard");
            ps3_draw_text(rx + 20, content_y + 364, 0xFFDCE8FCu, 1, "- .sgx : NEC PC Engine SuperGrafx");
            ps3_draw_text(rx + 20, content_y + 392, 0xFFDCE8FCu, 1, "- .cue / .iso / .hcd : CD-ROM2 & Super CD");
            ps3_draw_text(rx + 20, content_y + 420, 0xFFDCE8FCu, 1, "- .zip : Compressed HuCard Archives");

            const int btns[] = { BTN_ICON_CROSS, BTN_ICON_TRIANGLE, BTN_ICON_SELECT, BTN_ICON_L1 };
            const char *lbls[] = { "Open / Launch", "Parent Folder", "Cycle HDD/USB Root", "Switch Tabs" };
            ps3_draw_footer_buttons(btns, lbls, 4);

        } else if (active_tab == TAB_STATES) {
            int lx = 32;
            int lw = 680;
            int rx = 728;
            int rw = PS3_SCREEN_W - rx - 32;

            ps3_draw_panel(lx, content_y, lw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(lx + 20, content_y + 12, 0xFF7CE8FFu, 2, "SAVE STATE SLOTS");
            ps3_draw_badge(lx + lw - 240, content_y + 14, g_rom_base, 0xFF1D4682u, 0xFF8AEBFFu);
            ps3_draw_hline(lx + 16, content_y + 44, lw - 32, 0xFF244374u);

            for (int s = 0; s < 10; s++) {
                int ry = content_y + 52 + s * 45;
                int has = cached_state_exists[s];
                ps3_draw_card_row(lx + 14, ry, lw - 28, 41, s == st_sel);
                char lbl[64];
                snprintf(lbl, sizeof(lbl), "State Slot %d   -   %s", s, g_rom_base);
                ps3_draw_text_shadow(lx + 22, ry + 9, (s == st_sel) ? 0xFFFFFFFFu : 0xFFD0DEFAu, 2, lbl);
                ps3_draw_badge(lx + lw - 110, ry + 9,
                               has ? "SAVED" : "EMPTY",
                               has ? 0xFF167C52u : 0xFF1E2D4Au,
                               has ? 0xFFFFFFFFu : 0xFF8EA8D0u);
            }

            ps3_draw_panel(rx, content_y, rw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(rx + 22, content_y + 14, 0xFFFF9E3Du, 2, "STATE THUMBNAIL PREVIEW");
            ps3_draw_thumb_box(rx + 28, content_y + 54, rw - 56, 280,
                               g_slot_thumb_valid ? g_slot_thumb_argb : NULL,
                               PS3_THUMB_W, PS3_THUMB_H,
                               cached_state_exists[st_sel] ? "NO THUMBNAIL" : "SLOT EMPTY");

            ps3_draw_text(rx + 28, content_y + 356, 0xFFDCE8FCu, 1, "Save states and thumbnails are stored in:");
            ps3_draw_text(rx + 28, content_y + 382, 0xFF7CE8FFu, 1, PS3_SAVES_DIR);
            ps3_draw_text(rx + 28, content_y + 416, 0xFF9AC2FAu, 1, "Tip: Press L2+R2 during gameplay to Quick Save/Load!");

            const int btns[] = { BTN_ICON_CROSS, BTN_ICON_TRIANGLE, BTN_ICON_L1 };
            const char *lbls[] = { "Launch & Load State", "Delete State", "Switch Tabs" };
            ps3_draw_footer_buttons(btns, lbls, 3);

        } else if (active_tab == TAB_SETTINGS) {
            int px = 120;
            int pw = PS3_SCREEN_W - 240;
            ps3_draw_panel(px, content_y, pw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(px + 24, content_y + 12, 0xFF7CE8FFu, 2, "DISPLAY, AUDIO & CONTROLLER SETTINGS");
            ps3_draw_hline(px + 20, content_y + 44, pw - 40, 0xFF244374u);

            int rx = px + 24;
            int rw = pw - 48;
            int ry = content_y + 54;
            int rh = 36;
            int rg = 41;

            char vol_str[32];
            snprintf(vol_str, sizeof(vol_str), "%d%%", g_cfg.audio_volume);

            ps3_draw_selector_row(rx, ry + 0 * rg, rw, rh, "Display Aspect Ratio", aspect_label(g_cfg.aspect), cfg_sel == 0);
            ps3_draw_selector_row(rx, ry + 1 * rg, rw, rh, "Video Scaling Filter", filter_label(g_cfg.filter), cfg_sel == 1);
            ps3_draw_switch_row(rx,   ry + 2 * rg, rw, rh, "CRT Scanlines Overlay", g_cfg.scanlines, cfg_sel == 2);
            ps3_draw_slider_row(rx,   ry + 3 * rg, rw, rh, "Master Audio Volume", g_cfg.audio_volume, vol_str, cfg_sel == 3);
            ps3_draw_selector_row(rx, ry + 4 * rg, rw, rh, "Controller Type",
                                  pad_mode_label(g_cfg.six_button_pad),
                                  cfg_sel == 4);
            ps3_draw_selector_row(rx, ry + 5 * rg, rw, rh, "HuCard Button Layout",
                                  g_cfg.swap_i_ii ? "Cross = I  /  Square = II" : "Circle = I  /  Cross = II",
                                  cfg_sel == 5);
            ps3_draw_selector_row(rx, ry + 6 * rg, rw, rh, "Turbo Fire Rate (L1 / R1)", turbo_label(g_cfg.turbo_rate), cfg_sel == 6);
            ps3_draw_switch_row(rx,   ry + 7 * rg, rw, rh, "On-Screen FPS Counter", g_cfg.show_fps, cfg_sel == 7);

            ps3_draw_card_row(rx, ry + 8 * rg, rw, rh, cfg_sel == 8);
            ps3_draw_text_shadow(rx + 18, ry + 8 * rg + 9, (cfg_sel == 8) ? 0xFFFFB870u : 0xFFD0DEFAu, 2, "Save Configuration to Disk (hugo.ini)");

            ps3_draw_card_row(rx, ry + 9 * rg, rw, rh, cfg_sel == 9);
            ps3_draw_text_shadow(rx + 18, ry + 9 * rg + 9, (cfg_sel == 9) ? 0xFFFF5A6Au : 0xFFD0DEFAu, 2, "Exit PCE-PS3 (Return to XMB)");

            const int btns[] = { BTN_ICON_DPAD, BTN_ICON_CROSS, BTN_ICON_L1 };
            const char *lbls[] = { "Change Setting", "Toggle / Save", "Switch Tabs" };
            ps3_draw_footer_buttons(btns, lbls, 3);

        } else if (active_tab == TAB_BIOS) {
            int px = 80;
            int pw = PS3_SCREEN_W - 160;
            ps3_draw_panel(px, content_y, pw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(px + 26, content_y + 14, 0xFF7CE8FFu, 2, "CD-ROM2 SYSTEM CARD & HARDWARE STATUS");
            ps3_draw_hline(px + 20, content_y + 46, pw - 40, 0xFF244374u);

            ps3_draw_card_row(px + 24, content_y + 64, pw - 48, 68, 1);
            ps3_draw_text_shadow(px + 44, content_y + 74, 0xFFFFFFFFu, 2, "Super CD-ROM2 System Card (syscard3.pce)");
            ps3_draw_text(px + 44, content_y + 102, 0xFF9AC2FAu, 1,
                          cached_has_bios ? cached_syscard : "Not found in /dev_hdd0/pce4ps3/bios/syscard3.pce");
            ps3_draw_badge(px + pw - 160, content_y + 84,
                           cached_has_bios ? "READY" : "MISSING",
                           cached_has_bios ? 0xFF167C52u : 0xFFB83828u, 0xFFFFFFFFu);

            ps3_draw_card_row(px + 24, content_y + 148, pw - 48, 68, 0);
            ps3_draw_text_shadow(px + 44, content_y + 158, 0xFFD0DEFAu, 2, "NEC SuperGrafx Dual-VDC Engine (.sgx)");
            ps3_draw_text(px + 44, content_y + 186, 0xFF9AC2FAu, 1, "Built-in hardware emulation active automatically for .sgx ROMs");
            ps3_draw_badge(px + pw - 160, content_y + 168, "BUILT-IN", 0xFF167C52u, 0xFFFFFFFFu);

            ps3_draw_card_row(px + 24, content_y + 232, pw - 48, 68, 0);
            ps3_draw_text_shadow(px + 44, content_y + 242, 0xFFD0DEFAu, 2, "Backup RAM (BRAM) Auto-Save System");
            ps3_draw_text(px + 44, content_y + 270, 0xFF9AC2FAu, 1, "Per-game 8KB battery-backed save RAM stored in /dev_hdd0/pce4ps3/bram");
            ps3_draw_badge(px + pw - 160, content_y + 252, "ACTIVE", 0xFF167C52u, 0xFFFFFFFFu);

            ps3_draw_panel(px + 24, content_y + 324, pw - 48, 146, 0xF014284Cu, 0xF00B162Cu, 0xFFFF8824u);
            ps3_draw_text_shadow(px + 44, content_y + 340, 0xFFFF9E3Du, 2, "ONE-CLICK USB BIOS AUTO-IMPORTER");
            ps3_draw_text(px + 44, content_y + 376, 0xFFFFFFFFu, 1, bios_msg);
            ps3_draw_btn_label(px + 44, content_y + 416, BTN_ICON_TRIANGLE,
                               "Scan USB Drives (/dev_usb000..007) & Import syscard3.pce Now", 0xFF7CE8FFu);

            const int btns[] = { BTN_ICON_TRIANGLE, BTN_ICON_L1 };
            const char *lbls[] = { "Auto-Import BIOS from USB", "Switch Tabs" };
            ps3_draw_footer_buttons(btns, lbls, 2);

        } else if (active_tab == TAB_CREDITS) {
            int lx = 48;
            int lw = 600;
            int rx = 672;
            int rw = PS3_SCREEN_W - rx - 48;

            ps3_draw_panel(lx, content_y, lw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(lx + 24, content_y + 14, 0xFF7CE8FFu, 2, "DUALSHOCK 3 CONTROLS & SHORTCUTS");
            ps3_draw_hline(lx + 20, content_y + 46, lw - 40, 0xFF244374u);

            int cy = content_y + 64;
            ps3_draw_btn_label(lx + 28, cy +   0, BTN_ICON_DPAD,     "PC Engine Directional Pad (D-Pad & Left Stick)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy +  44, BTN_ICON_CIRCLE,   "Button I  (Primary Action / Confirm)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy +  88, BTN_ICON_CROSS,    "Button II (Secondary Action / Jump)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy + 132, BTN_ICON_TRIANGLE, "Turbo I   (Rapid Fire Button I)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy + 176, BTN_ICON_SQUARE,   "Turbo II  (Rapid Fire Button II)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy + 220, BTN_ICON_START,    "RUN Button (Start / Pause)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy + 264, BTN_ICON_SELECT,   "SELECT Button (In Games)", 0xFFFFFFFFu);
            ps3_draw_btn_label(lx + 28, cy + 308, BTN_ICON_L2,       "+ R2  (or L3+R3 / SELECT+START) : In-Game Quick Menu", 0xFFFFB870u);
            ps3_draw_btn_label(lx + 28, cy + 352, BTN_ICON_L1,       "/ R1  : Previous / Next Tab in Frontend", 0xFF8CE8FFu);
            ps3_draw_text(lx + 28, cy + 396, 0xFF7CE8FFu, 1, "6-Button Arcade: Square=LP, Triangle=MP, R1=HP, Cross=LK, Circle=MK, R2=HK");
            ps3_draw_text(lx + 28, cy + 420, 0xFF9AC2FAu, 1, "Fighting Games: Auto-detected (SF2, Garou, Fatal Fury, Art of Fighting, etc.)");

            ps3_draw_panel(rx, content_y, rw, content_h, 0xEC0E1B34u, 0xEC081122u, 0xFF2B4F88u);
            ps3_draw_text_shadow(rx + 24, content_y + 14, 0xFFFF9E3Du, 2, "AUTHORS & CREDITS");
            ps3_draw_hline(rx + 20, content_y + 46, rw - 40, 0xFF244374u);

            ps3_draw_text_shadow(rx + 24, content_y + 58, 0xFFFFFFFFu, 2, "PCE-PS3  v1.0  (Hu-Go! Core)");
            ps3_draw_text(rx + 24, content_y + 90, 0xFFFFB870u, 1, "PlayStation 3 Port by theheroGAC (2026)");
            ps3_draw_text(rx + 24, content_y + 116, 0xFF8CE8FFu, 1, "PSL1GHT Modern SDK Integration");
            ps3_draw_text(rx + 24, content_y + 144, 0xFFDCE8FCu, 1, "- 48 kHz Stereo Audio Driver with Ring Buffer");
            ps3_draw_text(rx + 24, content_y + 170, 0xFFDCE8FCu, 1, "- HuC6280 CPU + HuC6270 VDC + HuC6260 VCE");
            ps3_draw_text(rx + 24, content_y + 196, 0xFFDCE8FCu, 1, "- CD-ROM2 / Super CD-ROM2 + ADPCM + Redbook CDDA");
            ps3_draw_text(rx + 24, content_y + 222, 0xFFDCE8FCu, 1, "- 5-Player Multitap Support via DualShock 3");
            ps3_draw_text(rx + 24, content_y + 248, 0xFFDCE8FCu, 1, "- 10 Save State Slots with Live Thumbnail Previews");
            ps3_draw_text(rx + 24, content_y + 276, 0xFF7CE8FFu, 1, "Core: Zeograd, David Ries, Alex Krasivsky, notaz");

            ps3_draw_hline(rx + 20, content_y + 306, rw - 40, 0xFF244374u);

            ps3_draw_panel(rx + 20, content_y + 326, rw - 40, 114, 0xF8321018u, 0xF81C080Eu, 0xFFFF4A5Au);
            ps3_draw_rect(rx + 22, content_y + 328, rw - 44, 3, 0xFFFF6070u);
            ps3_draw_text_shadow(rx + 36, content_y + 344, 0xFFFF5A6Au, 2, "EXIT PCE-PS3 (RETURN TO XMB)");
            ps3_draw_btn_label(rx + 36, content_y + 386, BTN_ICON_CROSS, "Press CROSS to cleanly exit to PlayStation 3 XMB", 0xFFFFFFFFu);

            const int btns[] = { BTN_ICON_CROSS, BTN_ICON_L1, BTN_ICON_R1 };
            const char *lbls[] = { "Exit to XMB", "Previous Tab", "Next Tab" };
            ps3_draw_footer_buttons(btns, lbls, 3);
        }

        ps3_video_present();
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, ps3_sysutil_cb, NULL);
    ioPadInit(7);

    ps3_ensure_directories();
    ps3_log_init();
    ps3_config_load();

    if (ps3_video_init() != 0) {
        ps3_log("FATAL: ps3_video_init failed\n");
        ps3_log_close();
        sysProcessExit(0);
        return 0;
    }

    ps3_draw_boot_splash("INITIALIZING 48KHZ STEREO AUDIO & HUC6280 CORE...", 35);
    ps3_audio_init();

    option.want_snd_freq         = PS3_AUDIO_RATE;
    option.want_stereo           = 1;
    option.configure_joypads     = 0;
    option.want_fullscreen       = 1;
    option.want_fullscreen_aspect = 1;
    option.window_size           = 1;
    strcpy(initial_path, PS3_ROMS_DIR);

    ps3_draw_boot_splash("SCANNING PC ENGINE & CD-ROM2 LIBRARY...", 75);
    run_main_frontend();

    ps3_return_to_xmb();
    return 0;
}
