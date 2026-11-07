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
    snprintf(g_cfg.bios_name, sizeof(g_cfg.bios_name), "syscard3.pce");
    snprintf(g_cfg.last_rom_dir, sizeof(g_cfg.last_rom_dir), "%s", PS3_ROMS_DIR);
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
        else if (!strcasecmp(key, "audio_volume")) g_cfg.audio_volume = atoi(val);
        else if (!strcasecmp(key, "show_fps"))     g_cfg.show_fps     = atoi(val) ? 1 : 0;
        else if (!strcasecmp(key, "bios_name"))    snprintf(g_cfg.bios_name, sizeof(g_cfg.bios_name), "%s", val);
        else if (!strcasecmp(key, "last_rom_dir")) snprintf(g_cfg.last_rom_dir, sizeof(g_cfg.last_rom_dir), "%s", val);
    }
    fclose(f);

    if (g_cfg.turbo_rate < 2 || g_cfg.turbo_rate > 10) g_cfg.turbo_rate = 3;
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
    fprintf(f, "audio_volume=%d\n", g_cfg.audio_volume);
    fprintf(f, "show_fps=%d\n",     g_cfg.show_fps);
    fprintf(f, "bios_name=%s\n",    g_cfg.bios_name);
    fprintf(f, "last_rom_dir=%s\n", g_cfg.last_rom_dir);
    fclose(f);
}
