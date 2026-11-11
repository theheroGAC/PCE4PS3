#include "ps3_platform.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

static void mkdir_ignore(const char *p)
{
    mkdir(p, 0777);
}

void ps3_ensure_directories(void)
{
    mkdir_ignore(PS3_DATA_ROOT);
    mkdir_ignore(PS3_ROMS_DIR);
    mkdir_ignore(PS3_BIOS_DIR);
    mkdir_ignore(PS3_SAVES_DIR);
    mkdir_ignore(PS3_BRAM_DIR);
    mkdir_ignore(PS3_SCREENSHOTS_DIR);
}

static int file_exists(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0 && S_ISREG(st.st_mode));
}

int ps3_find_syscard(char *out_path, size_t cap)
{
    const char *names[] = {
        g_cfg.bios_name[0] ? g_cfg.bios_name : "syscard3.pce",
        "syscard3.pce",
        "SYSCARD3.PCE",
        "syscard3u.pce",
        "syscard2.pce",
        "syscard.pce",
        "bios.pce",
        NULL
    };

    const char *dirs[] = {
        PS3_BIOS_DIR,
        PS3_DATA_ROOT,
        PS3_ROMS_DIR,
        NULL
    };

    char candidate[512];
    for (int d = 0; dirs[d]; d++) {
        for (int n = 0; names[n]; n++) {
            snprintf(candidate, sizeof(candidate), "%s/%s", dirs[d], names[n]);
            if (file_exists(candidate)) {
                snprintf(out_path, cap, "%s", candidate);
                return 1;
            }
        }
    }

    if (g_active_rom_path[0] && strncmp(g_active_rom_path, "/dev_usb", 8) == 0) {
        char usb_dir[64];
        snprintf(usb_dir, sizeof(usb_dir), "%.11s/pce4ps3/bios", g_active_rom_path);
        for (int n = 0; names[n]; n++) {
            snprintf(candidate, sizeof(candidate), "%s/%s", usb_dir, names[n]);
            if (file_exists(candidate)) {
                snprintf(out_path, cap, "%s", candidate);
                return 1;
            }
        }
    }
    return 0;
}
