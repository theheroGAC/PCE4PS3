#include "ps3_platform.h"
#include "ps3_btn_icons.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "pce.h"
#include "gfx.h"

extern unsigned char *osd_gfx_buffer;
extern unsigned char *XBuf;

volatile int g_xmb_menu_open = 0;

static SDL_Window   *g_window     = NULL;
static SDL_Renderer *g_renderer   = NULL;
static SDL_Texture  *g_screen_tex = NULL;
static uint32_t     *g_screen_fb  = NULL;
static int           g_out_w      = PS3_SCREEN_W;
static int           g_out_h      = PS3_SCREEN_H;

static uint32_t g_palette_argb[256];
static int g_last_game_w = 256;
static int g_last_game_h = 224;

static TTF_Font *g_fonts[3] = { NULL, NULL, NULL };

static char g_toast_msg[128] = {0};
static int  g_toast_frames   = 0;

static uint64_t g_fps_last_us = 0;
static int      g_fps_counter = 0;
static float    g_fps_value   = 60.0f;

static const uint8_t s_font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x6C,0x6C,0x48,0x00,0x00,0x00,0x00,0x00},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},{0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00},{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},{0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},{0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},{0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},{0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},{0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},{0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},{0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},{0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},{0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},{0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},{0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},{0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},{0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},{0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00},
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},{0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},{0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06},
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},{0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00},
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00},{0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00},{0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},{0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},{0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},{0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    {0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00},{0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8},
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C},{0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},{0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00},
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},{0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},{0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00},
    {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00},{0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},{0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00},
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},{0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC},
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00},{0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},{0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}
};

#define TTF_CACHE_SLOTS 192

typedef struct {
    int      valid;
    int      tier;
    uint32_t hash;
    uint32_t last_use;
    char     text[96];
    int      w;
    int      h;
    uint8_t *alpha;
} ttf_cache_entry_t;

static ttf_cache_entry_t g_ttf_cache[TTF_CACHE_SLOTS];
static uint32_t          g_ttf_use_clock = 1;

static uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static inline uint32_t blend_argb(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xFFu;
    if (sa == 255u) return src;
    if (sa == 0u)   return dst;
    uint32_t ia = 255u - sa;
    uint32_t r = (((src >> 16) & 0xFFu) * sa + ((dst >> 16) & 0xFFu) * ia) >> 8;
    uint32_t g = (((src >>  8) & 0xFFu) * sa + ((dst >>  8) & 0xFFu) * ia) >> 8;
    uint32_t b = (((src >>  0) & 0xFFu) * sa + ((dst >>  0) & 0xFFu) * ia) >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline void put_pixel_blend(int x, int y, uint32_t src_argb)
{
    if (!g_screen_fb) return;
    if ((unsigned)x >= (unsigned)PS3_SCREEN_W || (unsigned)y >= (unsigned)PS3_SCREEN_H) return;
    uint32_t *dst = &g_screen_fb[y * PS3_SCREEN_W + x];
    *dst = blend_argb(*dst, src_argb);
}

static uint32_t fnv1a_str(const char *s, int tier)
{
    uint32_t h = 2166136261u ^ (uint32_t)tier;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void ttf_cache_clear(void)
{
    int i;
    for (i = 0; i < TTF_CACHE_SLOTS; i++) {
        if (g_ttf_cache[i].alpha) {
            free(g_ttf_cache[i].alpha);
            g_ttf_cache[i].alpha = NULL;
        }
        g_ttf_cache[i].valid = 0;
    }
}

static void init_fonts(void)
{
    static const char *font_candidates[] = {
        PS3_PKG_USRDIR "/font.ttf",
        PS3_SKINS_DIR "/default.ttf",
        "/dev_hdd0/game/PCE4PS300/USRDIR/font.ttf",
        "/dev_hdd0/game/PCE4PS300/USRDIR/skins/default.ttf",
        "/dev_hdd0/game/PCES00001/USRDIR/skins/default.ttf",
        PS3_DATA_ROOT "/font.ttf",
        "/dev_flash/data/font/SCE-PS3-VR-R-LATIN.TTF",
        NULL
    };
    const int sizes[3] = { 16, 20, 28 };

    if (TTF_Init() < 0) {
        ps3_log("TTF_Init failed: %s\n", TTF_GetError());
        return;
    }

    for (int i = 0; font_candidates[i]; i++) {
        FILE *f = fopen(font_candidates[i], "rb");
        if (!f) continue;
        fclose(f);
        for (int t = 0; t < 3; t++) {
            g_fonts[t] = TTF_OpenFont(font_candidates[i], sizes[t]);
        }
        if (g_fonts[0]) {
            ps3_log("Loaded UI font: %s\n", font_candidates[i]);
            return;
        }
    }
    ps3_log("Using built-in fallback font\n");
}

int ps3_video_init(void)
{
    SDL_DisplayMode dm;
    const Uint32 wflags[] = { 0, SDL_WINDOW_SHOWN };
    const Uint32 rflags[] = {
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_ACCELERATED,
        SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_SOFTWARE,
        0
    };
    size_t i;

    g_screen_fb = (uint32_t *)calloc((size_t)PS3_SCREEN_W * PS3_SCREEN_H, sizeof(uint32_t));
    if (!g_screen_fb) return -1;

    for (int c = 0; c < 256; c++) {
        uint8_t r = (uint8_t)((c & 0x1C) << 1);
        uint8_t g = (uint8_t)((c & 0xe0) >> 2);
        uint8_t b = (uint8_t)((c & 0x03) << 4);
        if (c == 255) {
            r = 0x3f;
            g = 0x3f;
            b = 0x3f;
        }
        r = (uint8_t)((r << 2) | (r >> 4));
        g = (uint8_t)((g << 2) | (g >> 4));
        b = (uint8_t)((b << 2) | (b >> 4));
        g_palette_argb[c] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        ps3_log("SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
        return -1;
    }
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);

    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        g_out_w = dm.w;
        g_out_h = dm.h;
    } else {
        g_out_w = PS3_SCREEN_W;
        g_out_h = PS3_SCREEN_H;
    }

    for (i = 0; i < sizeof(wflags) / sizeof(wflags[0]) && !g_window; i++) {
        g_window = SDL_CreateWindow(
            PS3_APP_NAME,
            0, 0,
            g_out_w, g_out_h,
            wflags[i]
        );
    }
    if (!g_window) {
        g_out_w = PS3_SCREEN_W;
        g_out_h = PS3_SCREEN_H;
        g_window = SDL_CreateWindow(PS3_APP_NAME, 0, 0, g_out_w, g_out_h, 0);
    }
    if (!g_window) {
        ps3_log("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    for (i = 0; i < sizeof(rflags) / sizeof(rflags[0]) && !g_renderer; i++) {
        g_renderer = SDL_CreateRenderer(g_window, -1, rflags[i]);
    }
    if (!g_renderer) {
        ps3_log("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_SetRenderDrawColor(g_renderer, 8, 12, 24, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

    ps3_video_apply_filter();

    init_fonts();
    return 0;
}

void ps3_video_shutdown(void)
{
    ttf_cache_clear();
    for (int i = 0; i < 3; i++) {
        if (g_fonts[i]) {
            TTF_CloseFont(g_fonts[i]);
            g_fonts[i] = NULL;
        }
    }
    if (TTF_WasInit()) TTF_Quit();
    if (g_screen_tex) { SDL_DestroyTexture(g_screen_tex); g_screen_tex = NULL; }
    if (g_renderer)   { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
    if (g_window)     { SDL_DestroyWindow(g_window); g_window = NULL; }
    if (g_screen_fb)  { free(g_screen_fb); g_screen_fb = NULL; }
    SDL_Quit();
}

void ps3_video_apply_filter(void)
{
    if (!g_renderer) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                (g_cfg.filter == PS3_FILTER_LINEAR) ? "linear" : "nearest");
    if (g_screen_tex) {
        SDL_DestroyTexture(g_screen_tex);
        g_screen_tex = NULL;
    }
    g_screen_tex = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        PS3_SCREEN_W, PS3_SCREEN_H
    );
    if (g_screen_tex) {
        SDL_SetTextureBlendMode(g_screen_tex, SDL_BLENDMODE_NONE);
    }
}

void ps3_video_set_palette_entry(unsigned char idx,
                                 unsigned char r,
                                 unsigned char g,
                                 unsigned char b)
{
    g_palette_argb[idx] = 0xFF000000u |
                          ((uint32_t)r << 16) |
                          ((uint32_t)g << 8)  |
                          ((uint32_t)b);
}

void ps3_video_clear(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_screen_fb) return;
    uint32_t px = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    size_t total = (size_t)PS3_SCREEN_W * PS3_SCREEN_H;
    for (size_t i = 0; i < total; i++) {
        g_screen_fb[i] = px;
    }
}

void ps3_video_present(void)
{
    SDL_Rect dst;
    if (!g_renderer || !g_screen_tex || !g_screen_fb) return;

    dst.x = 0;
    dst.y = 0;
    dst.w = g_out_w;
    dst.h = g_out_h;

    SDL_UpdateTexture(g_screen_tex, NULL, g_screen_fb, PS3_SCREEN_W * (int)sizeof(uint32_t));
    SDL_RenderCopy(g_renderer, g_screen_tex, NULL, &dst);
    SDL_RenderPresent(g_renderer);
}

void ps3_draw_rect(int x, int y, int w, int h, uint32_t argb)
{
    if (!g_screen_fb || w <= 0 || h <= 0) return;
    uint32_t a = (argb >> 24) & 0xFFu;
    if (a == 0u) return;

    int x0 = (x < 0) ? 0 : x;
    int y0 = (y < 0) ? 0 : y;
    int x1 = (x + w > PS3_SCREEN_W) ? PS3_SCREEN_W : (x + w);
    int y1 = (y + h > PS3_SCREEN_H) ? PS3_SCREEN_H : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    if (a == 255u) {
        for (int py = y0; py < y1; py++) {
            uint32_t *row = &g_screen_fb[py * PS3_SCREEN_W + x0];
            for (int px = x0; px < x1; px++) {
                *row++ = argb;
            }
        }
    } else {
        for (int py = y0; py < y1; py++) {
            uint32_t *row = &g_screen_fb[py * PS3_SCREEN_W + x0];
            for (int px = x0; px < x1; px++) {
                *row = blend_argb(*row, argb);
                row++;
            }
        }
    }
}

void ps3_draw_hline(int x, int y, int w, uint32_t argb)
{
    ps3_draw_rect(x, y, w, 1, argb);
}

void ps3_draw_border(int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    ps3_draw_rect(x, y, w, 1, argb);
    ps3_draw_rect(x, y + h - 1, w, 1, argb);
    ps3_draw_rect(x, y, 1, h, argb);
    ps3_draw_rect(x + w - 1, y, 1, h, argb);
}

void ps3_draw_panel(int x, int y, int w, int h, uint32_t top_argb, uint32_t bot_argb, uint32_t border_argb)
{
    int ry;
    if (w <= 0 || h <= 0) return;

    int ta = (top_argb >> 24) & 0xFF;
    int tr = (top_argb >> 16) & 0xFF;
    int tg = (top_argb >>  8) & 0xFF;
    int tb = (top_argb >>  0) & 0xFF;

    int ba = (bot_argb >> 24) & 0xFF;
    int br = (bot_argb >> 16) & 0xFF;
    int bg = (bot_argb >>  8) & 0xFF;
    int bb = (bot_argb >>  0) & 0xFF;

    int denom = (h > 1) ? (h - 1) : 1;
    for (ry = 0; ry < h; ry++) {
        uint32_t ca = (uint32_t)(ta + ((ba - ta) * ry) / denom);
        uint32_t cr = (uint32_t)(tr + ((br - tr) * ry) / denom);
        uint32_t cg = (uint32_t)(tg + ((bg - tg) * ry) / denom);
        uint32_t cb = (uint32_t)(tb + ((bb - tb) * ry) / denom);
        uint32_t col = (ca << 24) | (cr << 16) | (cg << 8) | cb;
        ps3_draw_rect(x, y + ry, w, 1, col);
    }

    if ((border_argb >> 24) != 0) {
        ps3_draw_border(x, y, w, h, border_argb);
        ps3_draw_hline(x + 1, y + 1, w - 2, 0x24FFFFFFu);
    }
}

void ps3_draw_bevel_box(int x, int y, int w, int h, uint32_t fill_argb, uint32_t hi_argb, uint32_t lo_argb)
{
    if (w <= 0 || h <= 0) return;
    ps3_draw_rect(x, y, w, h, fill_argb);
    ps3_draw_rect(x, y, w, 2, hi_argb);
    ps3_draw_rect(x, y, 2, h, hi_argb);
    ps3_draw_rect(x, y + h - 2, w, 2, lo_argb);
    ps3_draw_rect(x + w - 2, y, 2, h, lo_argb);
}

void ps3_draw_card_row(int x, int y, int w, int h, int selected)
{
    if (selected) {
        ps3_draw_panel(x, y, w, h, 0xF01C3F72u, 0xF011274Au, 0xFF4AD2FFu);
        ps3_draw_rect(x, y, 5, h, 0xFFFF7A18u);
        ps3_draw_rect(x + 5, y + 1, 2, h - 2, 0x80FFE082u);
    } else {
        ps3_draw_panel(x, y, w, h, 0xC8101C33u, 0xC80B1426u, 0x552A436Cu);
    }
}

static ttf_cache_entry_t *get_ttf_entry(const char *text, int tier)
{
    if (!text || !text[0]) return NULL;
    if (tier < 0) tier = 0;
    if (tier > 2) tier = 2;
    TTF_Font *font = g_fonts[tier];
    if (!font) return NULL;

    uint32_t h = fnv1a_str(text, tier);
    int victim = 0;
    uint32_t oldest = 0xFFFFFFFFu;

    for (int i = 0; i < TTF_CACHE_SLOTS; i++) {
        if (g_ttf_cache[i].valid &&
            g_ttf_cache[i].tier == tier &&
            g_ttf_cache[i].hash == h &&
            strncmp(g_ttf_cache[i].text, text, sizeof(g_ttf_cache[i].text) - 1) == 0) {
            g_ttf_cache[i].last_use = ++g_ttf_use_clock;
            return &g_ttf_cache[i];
        }
        if (!g_ttf_cache[i].valid) {
            victim = i;
            oldest = 0;
        } else if (g_ttf_cache[i].last_use < oldest) {
            oldest = g_ttf_cache[i].last_use;
            victim = i;
        }
    }

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, white);
    if (!surf) return NULL;

    SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(surf);
    if (!conv) return NULL;

    ttf_cache_entry_t *slot = &g_ttf_cache[victim];
    if (slot->alpha) {
        free(slot->alpha);
        slot->alpha = NULL;
    }

    slot->w = conv->w;
    slot->h = conv->h;
    slot->alpha = (uint8_t *)malloc((size_t)conv->w * conv->h);
    if (!slot->alpha) {
        SDL_FreeSurface(conv);
        slot->valid = 0;
        return NULL;
    }

    for (int sy = 0; sy < conv->h; sy++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)conv->pixels + sy * conv->pitch);
        uint8_t *dst = &slot->alpha[sy * conv->w];
        for (int sx = 0; sx < conv->w; sx++) {
            dst[sx] = (uint8_t)((src[sx] >> 24) & 0xFFu);
        }
    }
    SDL_FreeSurface(conv);

    slot->valid = 1;
    slot->tier = tier;
    slot->hash = h;
    slot->last_use = ++g_ttf_use_clock;
    strncpy(slot->text, text, sizeof(slot->text) - 1);
    slot->text[sizeof(slot->text) - 1] = '\0';
    return slot;
}

static int scale_to_tier(int scale)
{
    if (scale <= 1) return 0;
    if (scale == 2) return 1;
    return 2;
}

static void draw_fallback_text(int x, int y, uint32_t argb, int scale, const char *text)
{
    if (!text) return;
    if (scale < 1) scale = 1;
    uint32_t base_a = (argb >> 24) & 0xFFu;
    if (base_a == 0u) return;

    int cx = x;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 127) c = '?';
        const uint8_t *glyph = s_font8x8[c - 32];
        for (int gy = 0; gy < 8; gy++) {
            uint8_t bits = glyph[gy];
            if (!bits) continue;
            for (int gx = 0; gx < 8; gx++) {
                if (bits & (0x80u >> gx)) {
                    ps3_draw_rect(cx + gx * scale, y + gy * scale, scale, scale, argb);
                }
            }
        }
        cx += 8 * scale;
    }
}

void ps3_draw_text(int x, int y, uint32_t argb, int scale, const char *text)
{
    if (!text || !text[0] || !g_screen_fb) return;
    uint32_t base_a = (argb >> 24) & 0xFFu;
    if (base_a == 0u) return;

    int tier = scale_to_tier(scale);
    ttf_cache_entry_t *entry = get_ttf_entry(text, tier);
    if (!entry) {
        draw_fallback_text(x, y, argb, scale, text);
        return;
    }

    uint32_t rgb = argb & 0x00FFFFFFu;
    for (int sy = 0; sy < entry->h; sy++) {
        int dy = y + sy;
        if ((unsigned)dy >= (unsigned)PS3_SCREEN_H) continue;
        const uint8_t *arow = &entry->alpha[sy * entry->w];
        for (int sx = 0; sx < entry->w; sx++) {
            uint32_t a = arow[sx];
            if (!a) continue;
            int dx = x + sx;
            if ((unsigned)dx >= (unsigned)PS3_SCREEN_W) continue;
            uint32_t eff_a = (a * base_a) >> 8;
            if (eff_a) {
                put_pixel_blend(dx, dy, (eff_a << 24) | rgb);
            }
        }
    }
}

void ps3_draw_text_shadow(int x, int y, uint32_t fg_argb, int scale, const char *text)
{
    if (!text || !text[0]) return;
    ps3_draw_text(x + 2, y + 2, 0xCC040812u, scale, text);
    ps3_draw_text(x, y, fg_argb, scale, text);
}

int ps3_text_width(int scale, const char *text)
{
    if (!text || !text[0]) return 0;
    int tier = scale_to_tier(scale);
    ttf_cache_entry_t *entry = get_ttf_entry(text, tier);
    if (entry) return entry->w;
    if (scale < 1) scale = 1;
    return (int)strlen(text) * 8 * scale;
}

void ps3_draw_text_centered(int y, uint32_t argb, int scale, const char *text)
{
    int w = ps3_text_width(scale, text);
    ps3_draw_text_shadow((PS3_SCREEN_W - w) / 2, y, argb, scale, text);
}

void ps3_draw_text_clipped(int x, int y, int max_w, uint32_t argb, int scale, const char *text)
{
    if (!text || !text[0] || max_w <= 0) return;
    if (ps3_text_width(scale, text) <= max_w) {
        ps3_draw_text_shadow(x, y, argb, scale, text);
        return;
    }

    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t len = strlen(buf);
    while (len > 4) {
        len--;
        buf[len - 3] = '.';
        buf[len - 2] = '.';
        buf[len - 1] = '.';
        buf[len]     = '\0';
        if (ps3_text_width(scale, buf) <= max_w) break;
    }
    ps3_draw_text_shadow(x, y, argb, scale, buf);
}

int ps3_draw_badge(int x, int y, const char *label, uint32_t bg_argb, uint32_t fg_argb)
{
    if (!label || !label[0]) return 0;
    int tw = ps3_text_width(1, label);
    int bw = tw + 16;
    int bh = 22;
    ps3_draw_bevel_box(x, y, bw, bh, bg_argb, 0x55FFFFFFu, 0x66000000u);
    ps3_draw_text(x + 8, y + 2, fg_argb, 1, label);
    return bw;
}

void ps3_draw_selector_row(int x, int y, int w, int h, const char *label, const char *value_str, int selected)
{
    ps3_draw_card_row(x, y, w, h, selected);
    uint32_t lcol = selected ? 0xFFFFFFFFu : 0xFFD0DEFAu;
    ps3_draw_text_shadow(x + 18, y + (h - 20) / 2, lcol, 2, label);

    int pill_w = 270;
    int pill_h = h - 10;
    int pill_x = x + w - pill_w - 14;
    int pill_y = y + 5;
    uint32_t pbg = selected ? 0xFF0E2242u : 0xCC091326u;
    uint32_t pbd = selected ? 0xFF4AD2FFu : 0x662D4A78u;
    ps3_draw_panel(pill_x, pill_y, pill_w, pill_h, pbg, pbg, pbd);

    ps3_draw_text(pill_x + 10, pill_y + (pill_h - 18) / 2, selected ? 0xFFFF9E3Du : 0xFF6B88B8u, 1, "<");
    ps3_draw_text(pill_x + pill_w - 18, pill_y + (pill_h - 18) / 2, selected ? 0xFFFF9E3Du : 0xFF6B88B8u, 1, ">");

    int vw = ps3_text_width(1, value_str);
    ps3_draw_text_shadow(pill_x + (pill_w - vw) / 2, pill_y + (pill_h - 18) / 2,
                         selected ? 0xFF7CE8FFu : 0xFFC8D8F2u, 1, value_str);
}

void ps3_draw_switch_row(int x, int y, int w, int h, const char *label, int is_on, int selected)
{
    ps3_draw_card_row(x, y, w, h, selected);
    uint32_t lcol = selected ? 0xFFFFFFFFu : 0xFFD0DEFAu;
    ps3_draw_text_shadow(x + 18, y + (h - 20) / 2, lcol, 2, label);

    int sw_w = 112;
    int sw_h = 26;
    int sw_x = x + w - sw_w - 18;
    int sw_y = y + (h - sw_h) / 2;

    if (is_on) {
        ps3_draw_bevel_box(sw_x, sw_y, sw_w, sw_h, 0xFF167C52u, 0xFF46E0A2u, 0xFF0A3D28u);
        ps3_draw_bevel_box(sw_x + sw_w - 28, sw_y + 3, 24, sw_h - 6, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFBFD4EAu);
        ps3_draw_text_shadow(sw_x + 16, sw_y + 4, 0xFFFFFFFFu, 1, "ON");
    } else {
        ps3_draw_bevel_box(sw_x, sw_y, sw_w, sw_h, 0xFF182238u, 0xFF33476Eu, 0xFF0A101Cu);
        ps3_draw_bevel_box(sw_x + 4, sw_y + 3, 24, sw_h - 6, 0xFF8EA4C8u, 0xFFBFD2EEu, 0xFF516485u);
        ps3_draw_text_shadow(sw_x + 48, sw_y + 4, 0xFF9AB0D4u, 1, "OFF");
    }
}

void ps3_draw_slider_row(int x, int y, int w, int h, const char *label, int pct, const char *val_str, int selected)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ps3_draw_card_row(x, y, w, h, selected);
    uint32_t lcol = selected ? 0xFFFFFFFFu : 0xFFD0DEFAu;
    ps3_draw_text_shadow(x + 18, y + (h - 20) / 2, lcol, 2, label);

    int bar_w = 190;
    int bar_h = 14;
    int bar_x = x + w - bar_w - 86;
    int bar_y = y + (h - bar_h) / 2;

    ps3_draw_bevel_box(bar_x, bar_y, bar_w, bar_h, 0xFF0B1426u, 0xFF1F345Au, 0xFF060B14u);
    int fill_w = ((bar_w - 4) * pct) / 100;
    if (fill_w > 0) {
        ps3_draw_panel(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 0xFFFF942Eu, 0xFFE85D12u, 0x00000000u);
    }
    if (val_str) {
        ps3_draw_text_shadow(bar_x + bar_w + 12, y + (h - 18) / 2,
                             selected ? 0xFF7CE8FFu : 0xFFC8D8F2u, 1, val_str);
    }
}

int ps3_draw_btn_icon(int x, int y, int btn_type)
{
    if (btn_type < 0 || btn_type >= BTN_ICON_COUNT) btn_type = BTN_ICON_CROSS;
    const uint32_t *icon = ps3_btn_icons_data[btn_type];
    int w = PS3_BTN_ICON_SIZE;

    for (int iy = 0; iy < PS3_BTN_ICON_SIZE; iy++) {
        for (int ix = 0; ix < PS3_BTN_ICON_SIZE; ix++) {
            uint32_t px = icon[iy * PS3_BTN_ICON_SIZE + ix];
            if ((px >> 24) != 0) {
                put_pixel_blend(x + ix, y + iy, px);
            }
        }
    }
    return w;
}

int ps3_draw_btn_label(int x, int y, int btn_type, const char *label, uint32_t text_argb)
{
    int iw = ps3_draw_btn_icon(x, y, btn_type);
    ps3_draw_text_shadow(x + iw + 6, y + 3, text_argb, 1, label);
    return iw + 6 + ps3_text_width(1, label);
}

void ps3_draw_tab_bar(int y, const char **tabs, int tab_count, int active_tab)
{
    int bar_x = 32;
    int bar_w = PS3_SCREEN_W - 64;
    int bar_h = 38;

    ps3_draw_panel(bar_x, y, bar_w, bar_h, 0xE6101E38u, 0xE60A1324u, 0xFF284678u);
    ps3_draw_btn_label(bar_x + 12, y + 7, BTN_ICON_L1, "", 0xFF9AC2FAu);
    ps3_draw_btn_icon(bar_x + bar_w - 36, y + 7, BTN_ICON_R1);

    int inner_x = bar_x + 48;
    int inner_w = bar_w - 96;
    int tab_w = inner_w / (tab_count > 0 ? tab_count : 1);

    for (int i = 0; i < tab_count; i++) {
        int tx = inner_x + i * tab_w;
        int tw = (i == tab_count - 1) ? (inner_x + inner_w - tx) : (tab_w - 4);
        if (i == active_tab) {
            ps3_draw_panel(tx, y + 3, tw, bar_h - 6, 0xFF235294u, 0xFF153462u, 0xFF56DCFFu);
            ps3_draw_rect(tx + 2, y + bar_h - 5, tw - 4, 3, 0xFFFF821Eu);
            int lw = ps3_text_width(1, tabs[i]);
            ps3_draw_text_shadow(tx + (tw - lw) / 2, y + 10, 0xFFFFFFFFu, 1, tabs[i]);
        } else {
            int lw = ps3_text_width(1, tabs[i]);
            ps3_draw_text_shadow(tx + (tw - lw) / 2, y + 10, 0xFF8CAAD8u, 1, tabs[i]);
        }
    }
}

void ps3_draw_pce_background(const char *subtitle_right)
{
    ps3_draw_panel(0, 0, PS3_SCREEN_W, PS3_SCREEN_H, 0xFF0A1326u, 0xFF050A14u, 0x00000000u);

    for (int gy = 76; gy < PS3_SCREEN_H - 64; gy += 36) {
        ps3_draw_hline(0, gy, PS3_SCREEN_W, 0x103868B0u);
    }
    for (int gx = 64; gx < PS3_SCREEN_W; gx += 64) {
        ps3_draw_rect(gx, 74, 1, PS3_SCREEN_H - 144, 0x0C3868B0u);
    }

    int hy = 16;
    int hh = 52;
    ps3_draw_panel(32, hy, PS3_SCREEN_W - 64, hh, 0xF414284Cu, 0xF40B162Cu, 0xFF4AD2FFu);
    ps3_draw_rect(32, hy + hh - 3, PS3_SCREEN_W - 64, 2, 0xFFFF7A18u);

    ps3_draw_bevel_box(48, hy + 9, 56, 34, 0xFFE85D12u, 0xFFFFB066u, 0xFF8C2F00u);
    ps3_draw_text_shadow(58, hy + 16, 0xFFFFFFFFu, 2, "PCE");

    ps3_draw_text_shadow(120, hy + 7, 0xFFFFFFFFu, 2, "PC ENGINE / TURBOGRAFX-16");
    ps3_draw_text(122, hy + 30, 0xFF7CE8FFu, 1, "NEC HuCARD  |  SUPERGRAFX  |  CD-ROM2 SYSTEM");

    if (subtitle_right && subtitle_right[0]) {
        int rw = ps3_text_width(1, subtitle_right);
        ps3_draw_badge(PS3_SCREEN_W - rw - 52, hy + 14, subtitle_right, 0xFF16325Eu, 0xFF9BE8FFu);
    }
}

void ps3_draw_footer_buttons(const int *btns, const char **labels, int count)
{
    int fy = 646;
    int fh = 40;
    ps3_draw_panel(32, fy, PS3_SCREEN_W - 64, fh, 0xF40C172Eu, 0xF4070E1Cu, 0xFF2D548Eu);
    ps3_draw_rect(32, fy, PS3_SCREEN_W - 64, 1, 0xFF4AD2FFu);

    int cx = 52;
    for (int i = 0; i < count; i++) {
        cx += ps3_draw_btn_label(cx, fy + 8, btns[i], labels[i], 0xFFDCE8FCu) + 26;
    }
}

void ps3_draw_media_card(int x, int y, int w, int h, const char *title, int media_type, const char *badge_str)
{
    ps3_draw_panel(x, y, w, h, 0xF2122342u, 0xF20A1326u, 0xFF345E9Cu);

    int art_x = x + 22;
    int art_y = y + 18;
    int art_w = w - 44;
    int art_h = h - 76;

    if (media_type == PCE_MEDIA_CDROM) {
        ps3_draw_bevel_box(art_x, art_y, art_w, art_h, 0xFF0D1A30u, 0xFF3A6CB0u, 0xFF060C18u);
        int cx = art_x + art_w / 2;
        int cy = art_y + art_h / 2;
        int r_outer = (art_h < art_w ? art_h : art_w) / 2 - 12;
        int r_inner = r_outer / 3;
        for (int dy = -r_outer; dy <= r_outer; dy++) {
            for (int dx = -r_outer; dx <= r_outer; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= r_outer * r_outer && d2 >= r_inner * r_inner) {
                    uint32_t col = ((dx + dy) & 8) ? 0xFF3CA8E8u : 0xFF1F66A4u;
                    if (d2 >= (r_outer - 3) * (r_outer - 3)) col = 0xFFA8ECFFu;
                    put_pixel_blend(cx + dx, cy + dy, col);
                }
            }
        }
        ps3_draw_badge(art_x + 12, art_y + 10, "SUPER CD-ROM2", 0xFFD95B12u, 0xFFFFFFFFu);
    } else {
        uint32_t body_col = (media_type == PCE_MEDIA_SGX) ? 0xFF1A2844u : 0xFFEDEFF5u;
        uint32_t text_col = (media_type == PCE_MEDIA_SGX) ? 0xFFFFFFFFu : 0xFF101A30u;
        uint32_t stripe_col = (media_type == PCE_MEDIA_SGX) ? 0xFF28B8FFu : 0xFFFF6E14u;

        ps3_draw_bevel_box(art_x, art_y, art_w, art_h, body_col, 0xFFFFFFFFu, 0xFF6A7894u);
        ps3_draw_rect(art_x + 8, art_y + 8, art_w - 16, 26, stripe_col);
        ps3_draw_text_shadow(art_x + 16, art_y + 12, 0xFFFFFFFFu, 1,
                             (media_type == PCE_MEDIA_SGX) ? "SUPERGRAFX HuCARD" : "NEC / HUDSON HuCARD");

        ps3_draw_panel(art_x + 14, art_y + 42, art_w - 28, art_h - 72,
                       0xFF15294Cu, 0xFF0B162Au, 0xFF3E70B6u);
        if (title && title[0]) {
            ps3_draw_text_clipped(art_x + 24, art_y + 58, art_w - 48, 0xFFFFFFFFu, 1, title);
        }

        int pin_w = (art_w - 40) / 16;
        for (int p = 0; p < 16; p++) {
            ps3_draw_rect(art_x + 20 + p * pin_w, art_y + art_h - 20, pin_w - 3, 14, 0xFFD8A838u);
        }
        (void)text_col;
    }

    if (badge_str && badge_str[0]) {
        ps3_draw_badge(x + 18, y + h - 48, badge_str, 0xFF1D4682u, 0xFF8AEBFFu);
    }
    if (title && title[0]) {
        ps3_draw_text_clipped(x + 18, y + h - 24, w - 36, 0xFFFFFFFFu, 1, title);
    }
}

void ps3_draw_thumb_box(int x, int y, int w, int h, const uint32_t *pixels, int pw, int ph, const char *empty_label)
{
    ps3_draw_bevel_box(x, y, w, h, 0xFF081020u, 0xFF3662A4u, 0xFF040811u);
    int ix = x + 4;
    int iy = y + 4;
    int iw = w - 8;
    int ih = h - 8;

    if (pixels && pw > 0 && ph > 0) {
        for (int dy = 0; dy < ih; dy++) {
            int sy = (dy * ph) / ih;
            const uint32_t *srow = &pixels[sy * pw];
            uint32_t *drow = &g_screen_fb[(iy + dy) * PS3_SCREEN_W + ix];
            for (int dx = 0; dx < iw; dx++) {
                int sx = (dx * pw) / iw;
                drow[dx] = srow[sx] | 0xFF000000u;
            }
        }
    } else if (empty_label && empty_label[0]) {
        int tw = ps3_text_width(1, empty_label);
        ps3_draw_text_shadow(ix + (iw - tw) / 2, iy + (ih - 18) / 2, 0xFF6E8AB8u, 1, empty_label);
    }
}

void ps3_draw_boot_splash(const char *status_line, int step_pct)
{
    ps3_draw_pce_background("BOOTLOADER");

    int box_w = 760;
    int box_h = 290;
    int box_x = (PS3_SCREEN_W - box_w) / 2;
    int box_y = (PS3_SCREEN_H - box_h) / 2;

    ps3_draw_panel(box_x, box_y, box_w, box_h, 0xF414284Cu, 0xF40A1428u, 0xFF4AD2FFu);
    ps3_draw_rect(box_x + 2, box_y + 2, box_w - 4, 4, 0xFFFF7A18u);

    ps3_draw_text_centered(box_y + 36, 0xFFFFFFFFu, 3, "PCE-PS3");
    ps3_draw_text_centered(box_y + 84, 0xFF7CE8FFu, 2, "NEC PC ENGINE & TURBOGRAFX-16");
    ps3_draw_text_centered(box_y + 118, 0xFF9AC2FAu, 1, "Hu-Go! Core - Cell Broadband Engine Port by theheroGAC");

    int bar_w = box_w - 140;
    int bar_h = 22;
    int bar_x = box_x + 70;
    int bar_y = box_y + 166;
    ps3_draw_bevel_box(bar_x, bar_y, bar_w, bar_h, 0xFF081120u, 0xFF284A80u, 0xFF040810u);

    if (step_pct < 0) step_pct = 0;
    if (step_pct > 100) step_pct = 100;
    int fill_w = ((bar_w - 6) * step_pct) / 100;
    if (fill_w > 0) {
        ps3_draw_panel(bar_x + 3, bar_y + 3, fill_w, bar_h - 6, 0xFFFF9636u, 0xFFE0550Cu, 0x00000000u);
    }

    if (status_line && status_line[0]) {
        ps3_draw_text_centered(box_y + 216, 0xFFD4E4FFu, 1, status_line);
    }
    ps3_video_present();
}

void ps3_show_error_dialog(const char *title, const char *msg1, const char *msg2)
{
    while (!g_app_should_quit) {
        uint32_t pressed = 0;
        ps3_pad_poll_menu(&pressed);
        if (pressed & (PS3_ACT_CONFIRM | PS3_ACT_CANCEL | PS3_ACT_START)) {
            break;
        }

        ps3_draw_pce_background("SYSTEM ALERT");

        int dw = 720;
        int dh = 260;
        int dx = (PS3_SCREEN_W - dw) / 2;
        int dy = (PS3_SCREEN_H - dh) / 2;

        ps3_draw_panel(dx, dy, dw, dh, 0xF6201014u, 0xF610080Cu, 0xFFFF4455u);
        ps3_draw_rect(dx + 2, dy + 2, dw - 4, 4, 0xFFFF3848u);

        ps3_draw_text_centered(dy + 32, 0xFFFF5A6Au, 3, title ? title : "ERROR");
        ps3_draw_hline(dx + 30, dy + 74, dw - 60, 0xFF4A1822u);

        if (msg1 && msg1[0]) {
            ps3_draw_text_centered(dy + 96, 0xFFFFFFFFu, 2, msg1);
        }
        if (msg2 && msg2[0]) {
            ps3_draw_text_centered(dy + 140, 0xFFD0DCEEu, 1, msg2);
        }

        const int btns[] = { BTN_ICON_CROSS };
        const char *lbls[] = { "Press Cross to Continue" };
        ps3_draw_footer_buttons(btns, lbls, 1);

        ps3_video_present();
        sysUsleep(16000);
    }
}

void ps3_ui_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t argb = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    ps3_draw_rect(x, y, w, h, argb);
}

void ps3_ui_outline(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t argb = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    ps3_draw_border(x, y, w, h, argb);
}

int ps3_ui_text(int x, int y, const char *text, uint8_t r, uint8_t g, uint8_t b, int size_tier)
{
    uint32_t argb = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    int scale = size_tier + 1;
    ps3_draw_text_shadow(x, y, argb, scale, text);
    return ps3_text_width(scale, text);
}

int ps3_ui_text_width(const char *text, int size_tier)
{
    return ps3_text_width(size_tier + 1, text);
}

void ps3_ui_button_icon(int which, int x, int y, int size)
{
    (void)size;
    ps3_draw_btn_icon(x, y, which);
}

void ps3_ui_header_bar(const char *title, const char *subtitle)
{
    (void)title;
    ps3_draw_pce_background(subtitle);
}

void ps3_ui_footer_bar(const char *hints)
{
    int fy = PS3_SCREEN_H - 42;
    ps3_draw_panel(0, fy, PS3_SCREEN_W, 42, 0xF40C172Eu, 0xF4070E1Cu, 0x00000000u);
    ps3_draw_hline(0, fy, PS3_SCREEN_W, 0xFF2D548Eu);
    if (hints) {
        ps3_draw_text_shadow(32, fy + 11, 0xFFDCE8FCu, 1, hints);
    }
}

void ps3_video_toast(const char *msg, int frames)
{
    if (!msg) {
        g_toast_frames = 0;
        return;
    }
    strncpy(g_toast_msg, msg, sizeof(g_toast_msg) - 1);
    g_toast_msg[sizeof(g_toast_msg) - 1] = '\0';
    g_toast_frames = (frames > 0) ? frames : 120;
}

void ps3_video_capture_thumb(uint32_t *dst_argb, int dst_w, int dst_h)
{
    if (!dst_argb || dst_w <= 0 || dst_h <= 0) return;
    int src_w = g_last_game_w;
    int src_h = g_last_game_h;
    if (src_w <= 0 || src_w > XBUF_WIDTH)  src_w = 256;
    if (src_h <= 0 || src_h > XBUF_HEIGHT) src_h = 224;

    unsigned char *src_base = osd_gfx_buffer ? osd_gfx_buffer
                                             : (XBuf ? (XBuf + 32 + 64 * XBUF_WIDTH) : NULL);
    if (!src_base) {
        memset(dst_argb, 0, (size_t)dst_w * dst_h * sizeof(uint32_t));
        return;
    }

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * src_h) / dst_h;
        const unsigned char *srow = src_base + sy * XBUF_WIDTH;
        uint32_t *drow = &dst_argb[dy * dst_w];
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * src_w) / dst_w;
            drow[dx] = g_palette_argb[srow[sx]];
        }
    }
}

void ps3_video_render_game_to_fb(int src_w, int src_h)
{
    if (!g_screen_fb) return;

    if (src_w <= 0 || src_w > XBUF_WIDTH)  src_w = 256;
    if (src_h <= 0 || src_h > XBUF_HEIGHT) src_h = 224;
    g_last_game_w = src_w;
    g_last_game_h = src_h;

    unsigned char *src_base = osd_gfx_buffer ? osd_gfx_buffer
                                             : (XBuf ? (XBuf + 32 + 64 * XBUF_WIDTH) : NULL);
    if (!src_base) return;

    int dst_w = PS3_SCREEN_W;
    int dst_h = PS3_SCREEN_H;

    if (g_cfg.aspect == PS3_ASPECT_4_3) {
        dst_h = PS3_SCREEN_H;
        dst_w = (PS3_SCREEN_H * 4) / 3;
    } else if (g_cfg.aspect == PS3_ASPECT_PIXEL) {
        int scale_y = PS3_SCREEN_H / src_h;
        if (scale_y < 1) scale_y = 1;
        dst_h = src_h * scale_y;
        int scale_x = (dst_h * 4) / (3 * src_w);
        if (scale_x < 1) scale_x = 1;
        dst_w = src_w * scale_x;
        if (dst_w > PS3_SCREEN_W) dst_w = PS3_SCREEN_W;
    }

    int off_x = (PS3_SCREEN_W - dst_w) / 2;
    int off_y = (PS3_SCREEN_H - dst_h) / 2;

    if (off_x > 0) {
        for (int y = 0; y < PS3_SCREEN_H; y++) {
            memset(&g_screen_fb[y * PS3_SCREEN_W], 0, (size_t)off_x * sizeof(uint32_t));
            memset(&g_screen_fb[y * PS3_SCREEN_W + off_x + dst_w], 0, (size_t)off_x * sizeof(uint32_t));
        }
    }
    if (off_y > 0) {
        memset(&g_screen_fb[0], 0, (size_t)off_y * PS3_SCREEN_W * sizeof(uint32_t));
        memset(&g_screen_fb[(off_y + dst_h) * PS3_SCREEN_W], 0, (size_t)off_y * PS3_SCREEN_W * sizeof(uint32_t));
    }

    int scanline_darken = g_cfg.scanlines ? 1 : 0;

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * src_h) / dst_h;
        const unsigned char *srow = src_base + sy * XBUF_WIDTH;
        uint32_t *drow = &g_screen_fb[(off_y + dy) * PS3_SCREEN_W + off_x];

        if (scanline_darken && (dy & 1)) {
            for (int dx = 0; dx < dst_w; dx++) {
                int sx = (dx * src_w) / dst_w;
                uint32_t c = g_palette_argb[srow[sx]];
                uint32_t r = ((c >> 16) & 0xFFu) * 3 / 4;
                uint32_t g = ((c >>  8) & 0xFFu) * 3 / 4;
                uint32_t b = ( c        & 0xFFu) * 3 / 4;
                drow[dx] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        } else {
            for (int dx = 0; dx < dst_w; dx++) {
                int sx = (dx * src_w) / dst_w;
                drow[dx] = g_palette_argb[srow[sx]];
            }
        }
    }
}

void ps3_video_present_game_frame(int src_w, int src_h)
{
    if (!g_screen_fb) return;

    ps3_video_render_game_to_fb(src_w, src_h);

    g_fps_counter++;
    uint64_t t = now_us();
    if (g_fps_last_us == 0) {
        g_fps_last_us = t;
    } else if (t - g_fps_last_us >= 500000ULL) {
        g_fps_value = (float)g_fps_counter * 1000000.0f / (float)(t - g_fps_last_us);
        g_fps_counter = 0;
        g_fps_last_us = t;
    }

    if (g_cfg.show_fps) {
        char fps_buf[32];
        snprintf(fps_buf, sizeof(fps_buf), "%.1f FPS", g_fps_value);
        int w = ps3_text_width(1, fps_buf) + 20;
        ps3_draw_panel(PS3_SCREEN_W - w - 20, 16, w, 28, 0xCC0D1B34u, 0xCC071020u, 0xFF3E78C0u);
        ps3_draw_text_shadow(PS3_SCREEN_W - w - 10, 20, 0xFF7CE8FFu, 1, fps_buf);
    }

    if (g_toast_frames > 0 && g_toast_msg[0]) {
        int tw = ps3_text_width(2, g_toast_msg) + 40;
        int tx = (PS3_SCREEN_W - tw) / 2;
        int ty = PS3_SCREEN_H - 84;
        ps3_draw_panel(tx, ty, tw, 40, 0xE816325Eu, 0xE80C1C38u, 0xFFFF8824u);
        ps3_draw_text_shadow(tx + 20, ty + 9, 0xFFFFFFFFu, 2, g_toast_msg);
        g_toast_frames--;
    }

    ps3_video_present();
}
