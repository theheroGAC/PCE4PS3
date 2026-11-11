#include "ps3_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

ps3_config_t g_cfg;

void ps3_config_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.aspect       = PS3_ASPECT_4_3;
    g_cfg.filter       = PS3_FILTER_NEAREST;
    g_cfg.scanlines    = 0;
    g_cfg.swap_i_ii    = 0;
    g_cfg.turbo_rate   = 3;
    g_cfg.audio_volume = 100;
    g_cfg.show_fps     = 0;
    g_cfg.six_button_pad = PS3_PAD_AUTO;
    g_cfg.map_btn_i       = PS3_BTN_CIRCLE;
    g_cfg.map_btn_ii      = PS3_BTN_CROSS;
    g_cfg.map_turbo_i     = PS3_BTN_TRIANGLE;
    g_cfg.map_turbo_ii    = PS3_BTN_SQUARE;
    g_cfg.map_btn_iii     = PS3_BTN_R1;
    g_cfg.map_btn_iv      = PS3_BTN_SQUARE;
    g_cfg.map_btn_v       = PS3_BTN_TRIANGLE;
    g_cfg.map_btn_vi      = PS3_BTN_R2;
    g_cfg.map_run         = PS3_BTN_START;
    g_cfg.map_select      = PS3_BTN_SELECT;
    g_cfg.map_fastforward = PS3_BTN_L3;
    g_cfg.fast_forward_speed = 3;
    snprintf(g_cfg.bios_name, sizeof(g_cfg.bios_name), "syscard3.pce");
    snprintf(g_cfg.last_rom_dir, sizeof(g_cfg.last_rom_dir), "%s", PS3_ROMS_DIR);
}

const char *ps3_btn_name(int btn_id)
{
    switch (btn_id) {
        case PS3_BTN_CROSS:    return "Cross (X)";
        case PS3_BTN_CIRCLE:   return "Circle (O)";
        case PS3_BTN_SQUARE:   return "Square ([])";
        case PS3_BTN_TRIANGLE: return "Triangle (^)";
        case PS3_BTN_L1:       return "L1";
        case PS3_BTN_R1:       return "R1";
        case PS3_BTN_L2:       return "L2";
        case PS3_BTN_R2:       return "R2";
        case PS3_BTN_L3:       return "L3";
        case PS3_BTN_R3:       return "R3";
        case PS3_BTN_START:    return "Start";
        case PS3_BTN_SELECT:   return "Select";
        default:               return "None";
    }
}

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || isspace((unsigned char)s[len - 1]))) {
        s[--len] = '\0';
    }
}

void ps3_config_load(void)
{
    ps3_config_defaults();
    FILE *f = fopen(PS3_CONFIG_PATH, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == ';' || line[0] == '[' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        while (*key && isspace((unsigned char)*key)) key++;
        while (*val && isspace((unsigned char)*val)) val++;
        trim(key);
        trim(val);

        if (!strcasecmp(key, "aspect"))            g_cfg.aspect       = atoi(val) % PS3_ASPECT_COUNT;
        else if (!strcasecmp(key, "filter"))       g_cfg.filter       = atoi(val) % PS3_FILTER_COUNT;
        else if (!strcasecmp(key, "scanlines"))    g_cfg.scanlines    = atoi(val) ? 1 : 0;
        else if (!strcasecmp(key, "swap_i_ii"))    g_cfg.swap_i_ii    = atoi(val) ? 1 : 0;
        else if (!strcasecmp(key, "turbo_rate"))   g_cfg.turbo_rate   = atoi(val);
        else if (!strcasecmp(key, "audio_volume"))   g_cfg.audio_volume   = atoi(val);
        else if (!strcasecmp(key, "show_fps"))       g_cfg.show_fps       = atoi(val) ? 1 : 0;
        else if (!strcasecmp(key, "six_button_pad")) {
            int p = atoi(val);
            g_cfg.six_button_pad = (p >= 0 && p < PS3_PAD_MODE_COUNT) ? p : PS3_PAD_AUTO;
        }
        else if (!strcasecmp(key, "map_btn_i"))       g_cfg.map_btn_i       = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_btn_ii"))      g_cfg.map_btn_ii      = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_turbo_i"))     g_cfg.map_turbo_i     = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_turbo_ii"))    g_cfg.map_turbo_ii    = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_btn_iii"))     g_cfg.map_btn_iii     = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_btn_iv"))      g_cfg.map_btn_iv      = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_btn_v"))       g_cfg.map_btn_v       = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_btn_vi"))      g_cfg.map_btn_vi      = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_run"))         g_cfg.map_run         = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_select"))      g_cfg.map_select      = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "map_fastforward")) g_cfg.map_fastforward = atoi(val) % PS3_BTN_MAX;
        else if (!strcasecmp(key, "fast_forward_speed")) g_cfg.fast_forward_speed = atoi(val);
        else if (!strcasecmp(key, "bios_name"))    snprintf(g_cfg.bios_name, sizeof(g_cfg.bios_name), "%s", val);
        else if (!strcasecmp(key, "last_rom_dir")) snprintf(g_cfg.last_rom_dir, sizeof(g_cfg.last_rom_dir), "%s", val);
    }
    fclose(f);

    if (g_cfg.map_fastforward == PS3_BTN_R2) g_cfg.map_fastforward = PS3_BTN_L3;
    if (g_cfg.turbo_rate < 2 || g_cfg.turbo_rate > 10) g_cfg.turbo_rate = 3;
    if (g_cfg.fast_forward_speed < 2 || g_cfg.fast_forward_speed > 5) g_cfg.fast_forward_speed = 3;
    if (g_cfg.audio_volume < 0)   g_cfg.audio_volume = 0;
    if (g_cfg.audio_volume > 100) g_cfg.audio_volume = 100;
}

void ps3_config_save(void)
{
    FILE *f = fopen(PS3_CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "[hugo_ps3]\n");
    fprintf(f, "aspect=%d\n",       g_cfg.aspect);
    fprintf(f, "filter=%d\n",       g_cfg.filter);
    fprintf(f, "scanlines=%d\n",    g_cfg.scanlines);
    fprintf(f, "swap_i_ii=%d\n",    g_cfg.swap_i_ii);
    fprintf(f, "turbo_rate=%d\n",   g_cfg.turbo_rate);
    fprintf(f, "audio_volume=%d\n",   g_cfg.audio_volume);
    fprintf(f, "show_fps=%d\n",       g_cfg.show_fps);
    fprintf(f, "six_button_pad=%d\n", g_cfg.six_button_pad);
    fprintf(f, "map_btn_i=%d\n",       g_cfg.map_btn_i);
    fprintf(f, "map_btn_ii=%d\n",      g_cfg.map_btn_ii);
    fprintf(f, "map_turbo_i=%d\n",     g_cfg.map_turbo_i);
    fprintf(f, "map_turbo_ii=%d\n",    g_cfg.map_turbo_ii);
    fprintf(f, "map_btn_iii=%d\n",     g_cfg.map_btn_iii);
    fprintf(f, "map_btn_iv=%d\n",      g_cfg.map_btn_iv);
    fprintf(f, "map_btn_v=%d\n",       g_cfg.map_btn_v);
    fprintf(f, "map_btn_vi=%d\n",      g_cfg.map_btn_vi);
    fprintf(f, "map_run=%d\n",         g_cfg.map_run);
    fprintf(f, "map_select=%d\n",      g_cfg.map_select);
    fprintf(f, "map_fastforward=%d\n", g_cfg.map_fastforward);
    fprintf(f, "fast_forward_speed=%d\n", g_cfg.fast_forward_speed);
    fprintf(f, "bios_name=%s\n",    g_cfg.bios_name);
    fprintf(f, "last_rom_dir=%s\n", g_cfg.last_rom_dir);
    fclose(f);
}
