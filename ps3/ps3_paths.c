#include "ps3_platform.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

static int str_contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !needle[0]) return 0;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return 1;
    }
    return 0;
}

static int has_syscard_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return (strcasecmp(dot, ".pce") == 0 ||
            strcasecmp(dot, ".bin") == 0 ||
            strcasecmp(dot, ".rom") == 0);
}

int ps3_score_syscard_file(const char *path, int strict_dir)
{
    if (!path || !path[0]) return 0;

    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    if (!has_syscard_ext(base)) return 0;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;

    uint64_t sz = (uint64_t)st.st_size;
    if (sz < 65536ULL || sz > 524800ULL) return 0;

    int has_kw = str_contains_ci(base, "syscard") ||
                 str_contains_ci(base, "bios") ||
                 str_contains_ci(base, "system") ||
                 str_contains_ci(base, "cd-rom") ||
                 str_contains_ci(base, "cdrom") ||
                 str_contains_ci(base, "games_express") ||
                 str_contains_ci(base, "games express");

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t hdr_off = (uint32_t)(sz & 0x1FFFull);
    if (hdr_off > 0 && hdr_off != 512u) {
        fclose(f);
        return 0;
    }
    if (hdr_off > 0) fseek(f, (long)hdr_off, SEEK_SET);
    unsigned char hdr[16];
    size_t got = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (got < 15) return 0;

    int is_jmp_table = (hdr[0] == 0x4C && hdr[3] == 0x4C &&
                        hdr[6] == 0x4C && hdr[9] == 0x4C &&
                        hdr[12] == 0x4C);
    int is_hi_jmp = (is_jmp_table && hdr[2] >= 0xE0 && hdr[5] >= 0xE0 && hdr[8] >= 0xE0);

    if (strict_dir) {
        if (!has_kw && !is_hi_jmp) return 0;
    } else {
        if (!has_kw && !is_jmp_table && sz != 262144ULL && sz != 262656ULL) return 0;
    }

    int score = 10;
    if (sz == 262144ULL || sz == 262656ULL) {
        score += 100;
    } else if (sz == 131072ULL || sz == 131584ULL) {
        score += 50;
    }
    if (is_jmp_table) score += 30;
    if (is_hi_jmp)    score += 20;

    if (str_contains_ci(base, "syscard3") || str_contains_ci(base, "v3") ||
        str_contains_ci(base, "super")) {
        score += 60;
    } else if (str_contains_ci(base, "syscard2") || str_contains_ci(base, "v2")) {
        score += 30;
    } else if (has_kw) {
        score += 15;
    }

    if (g_cfg.bios_name[0] && strcasecmp(base, g_cfg.bios_name) == 0) {
        score += 200;
    } else if (strcasecmp(base, "syscard3.pce") == 0) {
        score += 150;
    }

    return score;
}

static void scan_dir_for_syscard(const char *dir_path, int strict_dir,
                                 char *best_path, size_t cap, int *best_score)
{
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    char cand[512];
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        snprintf(cand, sizeof(cand), "%s/%s", dir_path, de->d_name);
        int sc = ps3_score_syscard_file(cand, strict_dir);
        if (sc > *best_score) {
            *best_score = sc;
            snprintf(best_path, cap, "%s", cand);
        }
    }
    closedir(d);
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

    char best_path[512] = "";
    int best_score = 0;

    scan_dir_for_syscard(PS3_BIOS_DIR, 0, best_path, sizeof(best_path), &best_score);

    if (g_active_rom_path[0]) {
        char rom_dir[512];
        snprintf(rom_dir, sizeof(rom_dir), "%s", g_active_rom_path);
        char *sl = strrchr(rom_dir, '/');
        if (sl && sl > rom_dir) {
            *sl = '\0';
            scan_dir_for_syscard(rom_dir, 1, best_path, sizeof(best_path), &best_score);
        }
    }

    scan_dir_for_syscard(PS3_DATA_ROOT, 1, best_path, sizeof(best_path), &best_score);
    scan_dir_for_syscard(PS3_ROMS_DIR, 1, best_path, sizeof(best_path), &best_score);

    if (best_score <= 0) {
        static const char *usb_subs[] = {
            "/pce4ps3/bios", "/bios", "/BIOS", "/pce", "/PCE", "/roms/pce", "", NULL
        };
        for (int u = 0; u < 8; u++) {
            char uroot[32];
            snprintf(uroot, sizeof(uroot), "/dev_usb00%d", u);
            DIR *chk = opendir(uroot);
            if (!chk) continue;
            closedir(chk);
            for (int s = 0; usb_subs[s]; s++) {
                char udir[128];
                snprintf(udir, sizeof(udir), "/dev_usb00%d%s", u, usb_subs[s]);
                int strict = (s >= 3) ? 1 : 0;
                scan_dir_for_syscard(udir, strict, best_path, sizeof(best_path), &best_score);
            }
        }
    }

    if (best_score > 0 && best_path[0]) {
        snprintf(out_path, cap, "%s", best_path);
        return 1;
    }

    return 0;
}
