#include "ps3_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/thread.h>
#include <sys/systime.h>

#include "pce.h"
#include "cd.h"
#include "sys_cd.h"

extern UChar binbcd[];

typedef struct {
    FILE    *fp;
    char     bin_path[512];
    int      is_audio;
    uint32_t sector_size;
    uint32_t data_offset;
    uint32_t start_lba;
    uint32_t length_lba;
    uint64_t file_byte_offset;
    uint64_t file_data_base;
    uint64_t file_data_size;
    uint32_t idx1_file_lba;
    int      file_id;
} ps3_cd_track_t;

static ps3_cd_track_t g_tracks[100];
static int            g_num_tracks = 0;
static int            g_cue_loaded = 0;

static FILE    *g_last_data_fp = NULL;
static uint64_t g_last_data_pos = (uint64_t)-1;
static int      g_last_data_trk = 0;

#define CD_CACHE_BLK_SECTORS 8u
#define CD_CACHE_SLOTS       128u

typedef struct {
    uint32_t base_lba;
    uint32_t age;
    uint8_t  valid;
    uint8_t  data[CD_CACHE_BLK_SECTORS * 2048u];
} ps3_cd_cache_slot_t;

static ps3_cd_cache_slot_t g_cd_cache[CD_CACHE_SLOTS];
static uint32_t            g_cd_cache_tick = 1u;
static int                 g_cd_cache_last = -1;
static uint8_t             g_cd_batch_raw[CD_CACHE_BLK_SECTORS * 2352u];

#define CDDA_RING_FRAMES   32768u
#define CDDA_RING_MASK     (CDDA_RING_FRAMES - 1u)
#define CDDA_CHUNK_FRAMES  2048u

typedef struct {
    uint32_t gen;
    int      active;
    int      repeat;
    int      track;
    uint64_t byte_start;
    uint64_t byte_end;
    char     bin_path[512];
} cdda_cmd_t;

static cdda_cmd_t        g_cdda_cmd;
static volatile uint32_t g_cdda_cmd_seq = 0;
static volatile uint32_t g_cdda_active_gen = 0;
static volatile uint32_t g_cdda_ring_gen   = 0;
static volatile uint32_t g_cdda_ack_gen    = 0;
static volatile uint32_t g_cdda_eof_gen    = 0;

static int16_t           g_cdda_ring[CDDA_RING_FRAMES * 2u];
static volatile uint32_t g_cdda_wr = 0;
static volatile uint32_t g_cdda_rd = 0;

static sys_ppu_thread_t  g_cdda_tid = 0;
static volatile int      g_cdda_thread_started = 0;

static volatile int      g_cdda_playing = 0;
static volatile int      g_cdda_paused  = 0;
static int               g_cdda_track   = 0;
static uint64_t          g_cdda_byte_start = 0;
static uint64_t          g_cdda_byte_end   = 0;
static uint64_t          g_cdda_played_bytes = 0;
static uint32_t          g_cdda_phase_160 = 0;

static void trim_line(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || isspace((unsigned char)s[len - 1]))) {
        s[--len] = '\0';
    }
}

static char *skip_ws(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void lba_to_msf(uint32_t lba, UChar *m, UChar *s, UChar *f)
{
    uint32_t total = lba + 150u;
    uint32_t min = total / (75u * 60u);
    if (min > 99u) min = 99u;
    *m = (UChar)min;
    *s = (UChar)((total / 75u) % 60u);
    *f = (UChar)(total % 75u);
}

static uint32_t msf_to_lba_clamped(int m, int s, int f)
{
    uint32_t total = (uint32_t)m * 60u * 75u + (uint32_t)s * 75u + (uint32_t)f;
    return (total >= 150u) ? (total - 150u) : 0u;
}

static int bcd_byte_valid(uint8_t v)
{
    return ((v >> 4) <= 9) && ((v & 0x0F) <= 9);
}

static uint32_t bcd_byte_to_bin(uint8_t v)
{
    return (uint32_t)((v >> 4) * 10 + (v & 0x0F));
}

static int resolve_cue_file_path(const char *base_dir, const char *raw_fname,
                                 char *out_path, size_t cap)
{
    char norm[256];
    snprintf(norm, sizeof(norm), "%s", raw_fname);
    for (int i = 0; norm[i]; i++) {
        if (norm[i] == '\\') norm[i] = '/';
    }

    const char *base_only = strrchr(norm, '/');
    base_only = base_only ? base_only + 1 : norm;

    struct stat st;
    snprintf(out_path, cap, "%s/%s", base_dir, norm);
    if (stat(out_path, &st) == 0 && S_ISREG(st.st_mode)) return 0;

    if (base_only != norm) {
        snprintf(out_path, cap, "%s/%s", base_dir, base_only);
        if (stat(out_path, &st) == 0 && S_ISREG(st.st_mode)) return 0;
    }

    DIR *d = opendir(base_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strcasecmp(de->d_name, base_only) == 0) {
                snprintf(out_path, cap, "%s/%s", base_dir, de->d_name);
                if (stat(out_path, &st) == 0 && S_ISREG(st.st_mode)) {
                    closedir(d);
                    return 0;
                }
            }
        }
        closedir(d);
    }

    snprintf(out_path, cap, "%s/%s", base_dir, base_only);
    return -1;
}

static void detect_wav_bounds(FILE *fp, uint64_t fsize, uint64_t *out_base, uint64_t *out_size)
{
    *out_base = 0;
    *out_size = fsize;
    if (!fp || fsize < 44ULL) return;

    uint8_t hdr[12];
    fseek(fp, 0, SEEK_SET);
    if (fread(hdr, 1, 12, fp) != 12) return;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return;

    uint64_t pos = 12ULL;
    while (pos + 8ULL <= fsize) {
        uint8_t ch[8];
        fseek(fp, (long)pos, SEEK_SET);
        if (fread(ch, 1, 8, fp) != 8) break;
        uint32_t csz = (uint32_t)ch[4] | ((uint32_t)ch[5] << 8) |
                       ((uint32_t)ch[6] << 16) | ((uint32_t)ch[7] << 24);
        if (memcmp(ch, "data", 4) == 0) {
            *out_base = pos + 8ULL;
            uint64_t avail = fsize - *out_base;
            *out_size = ((uint64_t)csz <= avail) ? (uint64_t)csz : avail;
            return;
        }
        pos += 8ULL + (uint64_t)((csz + 1u) & ~1u);
    }
}

static uint64_t get_file_size_u64(const char *path, FILE *fp)
{
    struct stat st;
    if (path && stat(path, &st) == 0) {
        return (uint64_t)st.st_size;
    }
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    return (sz > 0) ? (uint64_t)sz : 0ULL;
}

static void populate_hugo_toc(void)
{
    nb_max_track = (UChar)g_num_tracks;
    for (int i = 1; i <= g_num_tracks; i++) {
        ps3_cd_track_t *tr = &g_tracks[i];
        lba_to_msf(tr->start_lba, &CD_track[i].beg_min, &CD_track[i].beg_sec, &CD_track[i].beg_fra);
        CD_track[i].type        = tr->is_audio ? 0 : 4;
        CD_track[i].beg_lsn     = tr->start_lba;
        CD_track[i].length      = tr->length_lba;
        CD_track[i].source_type = 0;
    }
}

static void ensure_cdda_thread(void);
static uint32_t cdda_send_cmd(int active, int repeat, int track,
                              uint64_t byte_start, uint64_t byte_end,
                              const char *bin_path);

static void cd_cache_invalidate(void)
{
    for (uint32_t i = 0; i < CD_CACHE_SLOTS; i++) {
        g_cd_cache[i].valid = 0;
        g_cd_cache[i].age = 0;
    }
    g_cd_cache_tick = 1u;
    g_cd_cache_last = -1;
}

void ps3_cue_unload(void)
{
    g_cdda_playing = 0;
    g_cdda_paused  = 0;
    if (g_cdda_thread_started) {
        uint32_t gen = cdda_send_cmd(0, 0, 0, 0ULL, 0ULL, "");
        int guard = 0;
        while (g_cdda_ack_gen != gen && guard < 1000) {
            sysUsleep(2000);
            guard++;
        }
    }
    g_last_data_fp    = NULL;
    g_last_data_pos   = (uint64_t)-1;
    g_last_data_trk   = 0;
    cd_cache_invalidate();
    for (int i = 1; i <= g_num_tracks; i++) {
        if (g_tracks[i].fp) {
            FILE *fp = g_tracks[i].fp;
            for (int j = i; j <= g_num_tracks; j++) {
                if (g_tracks[j].fp == fp) g_tracks[j].fp = NULL;
            }
            fclose(fp);
        }
    }
    memset(g_tracks, 0, sizeof(g_tracks));
    g_num_tracks = 0;
    g_cue_loaded = 0;
}

int ps3_cue_is_loaded(void)
{
    return g_cue_loaded;
}

int ps3_cue_load(const char *cue_path)
{
    ps3_cue_unload();

    FILE *cue = fopen(cue_path, "r");
    if (!cue) {
        ps3_log("Failed to open CUE: %s\n", cue_path);
        return -1;
    }

    char base_dir[512];
    snprintf(base_dir, sizeof(base_dir), "%s", cue_path);
    char *slash = strrchr(base_dir, '/');
    if (slash) *slash = '\0';
    else strcpy(base_dir, ".");

    char cur_file_path[512] = "";
    FILE *cur_fp = NULL;
    uint64_t cur_data_base = 0;
    uint64_t cur_data_size = 0;
    int cur_file_id = 0;

    int cur_track = 0;
    int prev_track = 0;
    uint32_t running_lba = 0;
    uint32_t file_base_disc_lba = 0;
    uint32_t cum_pregap_in_file = 0;
    uint32_t track_pregap = 0;
    uint32_t pending_postgap = 0;
    int has_idx0 = 0;
    uint32_t idx0_lba = 0;

    char line[512];
    while (fgets(line, sizeof(line), cue)) {
        trim_line(line);
        char *p = skip_ws(line);
        if (!*p) continue;

        if (!strncasecmp(p, "FILE", 4) && isspace((unsigned char)p[4])) {
            if (prev_track >= 1 && g_tracks[prev_track].file_id == cur_file_id &&
                cur_file_id > 0) {
                ps3_cd_track_t *pt = &g_tracks[prev_track];
                uint64_t file_end = pt->file_data_base + pt->file_data_size;
                uint32_t secs = 0;
                if (file_end > pt->file_byte_offset && pt->sector_size > 0) {
                    secs = (uint32_t)((file_end - pt->file_byte_offset) / pt->sector_size);
                }
                pt->length_lba = secs;
                running_lba = pt->start_lba + secs + pending_postgap;
                pending_postgap = 0;
            }

            char *q1 = strchr(p, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            char fname[256] = "";
            if (q1 && q2 && q2 > q1 + 1) {
                size_t n = (size_t)(q2 - q1 - 1);
                if (n >= sizeof(fname)) n = sizeof(fname) - 1;
                memcpy(fname, q1 + 1, n);
                fname[n] = '\0';
            } else {
                sscanf(p + 4, "%255s", fname);
            }

            resolve_cue_file_path(base_dir, fname, cur_file_path, sizeof(cur_file_path));
            cur_fp = fopen(cur_file_path, "rb");
            if (!cur_fp) {
                ps3_log("CUE referenced missing file: %s\n", cur_file_path);
                cur_data_base = 0;
                cur_data_size = 0;
            } else {
                setvbuf(cur_fp, NULL, _IOFBF, 32768);
                uint64_t fsz = get_file_size_u64(cur_file_path, cur_fp);
                detect_wav_bounds(cur_fp, fsz, &cur_data_base, &cur_data_size);
            }
            cur_file_id++;
            file_base_disc_lba = running_lba;
            cum_pregap_in_file = 0;
        } else if (!strncasecmp(p, "TRACK", 5) && isspace((unsigned char)p[5])) {
            int tnum = 0;
            char mode[64] = "";
            if (sscanf(p + 5, "%d %63s", &tnum, mode) == 2 && tnum >= 1 && tnum < 100) {
                prev_track = cur_track;
                cur_track = tnum;
                if (tnum > g_num_tracks) g_num_tracks = tnum;
                ps3_cd_track_t *tr = &g_tracks[cur_track];
                tr->fp = cur_fp;
                tr->file_id = cur_file_id;
                tr->file_data_base = cur_data_base;
                tr->file_data_size = cur_data_size;
                snprintf(tr->bin_path, sizeof(tr->bin_path), "%s", cur_file_path);
                if (!strcasecmp(mode, "AUDIO")) {
                    tr->is_audio    = 1;
                    tr->sector_size = 2352;
                    tr->data_offset = 0;
                } else if (!strncasecmp(mode, "MODE2/2352", 10)) {
                    tr->is_audio    = 0;
                    tr->sector_size = 2352;
                    tr->data_offset = 24;
                } else if (strstr(mode, "2352")) {
                    tr->is_audio    = 0;
                    tr->sector_size = 2352;
                    tr->data_offset = 16;
                } else {
                    tr->is_audio    = 0;
                    tr->sector_size = 2048;
                    tr->data_offset = 0;
                }
                track_pregap = 0;
                has_idx0 = 0;
                idx0_lba = 0;
            }
        } else if (!strncasecmp(p, "PREGAP", 6) && cur_track >= 1) {
            int mm = 0, ss = 0, ff = 0;
            if (sscanf(p + 6, "%d:%d:%d", &mm, &ss, &ff) == 3) {
                track_pregap += (uint32_t)(mm * 60 * 75 + ss * 75 + ff);
            }
        } else if (!strncasecmp(p, "POSTGAP", 7) && cur_track >= 1) {
            int mm = 0, ss = 0, ff = 0;
            if (sscanf(p + 7, "%d:%d:%d", &mm, &ss, &ff) == 3) {
                pending_postgap += (uint32_t)(mm * 60 * 75 + ss * 75 + ff);
            }
        } else if (!strncasecmp(p, "INDEX", 5) && cur_track >= 1) {
            int idx = 0, mm = 0, ss = 0, ff = 0;
            if (sscanf(p + 5, "%d %d:%d:%d", &idx, &mm, &ss, &ff) == 4) {
                uint32_t ilba = (uint32_t)(mm * 60 * 75 + ss * 75 + ff);
                if (idx == 0) {
                    has_idx0 = 1;
                    idx0_lba = ilba;
                } else if (idx == 1) {
                    ps3_cd_track_t *tr = &g_tracks[cur_track];
                    cum_pregap_in_file += track_pregap;
                    tr->idx1_file_lba = ilba;
                    tr->file_byte_offset = tr->file_data_base + (uint64_t)ilba * tr->sector_size;
                    tr->start_lba = file_base_disc_lba + cum_pregap_in_file + ilba;

                    if (prev_track >= 1 && g_tracks[prev_track].file_id == tr->file_id) {
                        uint32_t end_in_file = has_idx0 ? idx0_lba : ilba;
                        if (end_in_file >= g_tracks[prev_track].idx1_file_lba) {
                            g_tracks[prev_track].length_lba =
                                end_in_file - g_tracks[prev_track].idx1_file_lba;
                        }
                    }
                    track_pregap = 0;
                    has_idx0 = 0;
                }
            }
        }
    }
    fclose(cue);

    if (g_num_tracks < 1) {
        ps3_cue_unload();
        return -1;
    }

    ps3_cd_track_t *last = &g_tracks[g_num_tracks];
    if (last->fp) {
        uint64_t file_end = last->file_data_base + last->file_data_size;
        if (file_end > last->file_byte_offset && last->sector_size > 0) {
            last->length_lba = (uint32_t)((file_end - last->file_byte_offset) / last->sector_size);
        }
    }

    int has_readable_data = 0;
    for (int i = 1; i <= g_num_tracks; i++) {
        if (!g_tracks[i].fp) {
            ps3_log("CUE track %d binary missing: %s\n", i, g_tracks[i].bin_path);
            ps3_cue_unload();
            return -1;
        }
        if (!g_tracks[i].is_audio) has_readable_data = 1;
    }
    if (!has_readable_data) {
        ps3_log("CUE contains no readable data track: %s\n", cue_path);
        ps3_cue_unload();
        return -1;
    }

    cd_cache_invalidate();
    ensure_cdda_thread();
    populate_hugo_toc();
    g_cue_loaded = 1;
    ps3_log("CUE loaded: %s (%d tracks)\n", cue_path, g_num_tracks);
    return 0;
}

static int is_raw_mode_sync(const uint8_t *sec)
{
    static const uint8_t sync[12] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
    };
    return (memcmp(sec, sync, 12) == 0 && (sec[15] == 1 || sec[15] == 2));
}

int ps3_cd_open_image(const char *img_path)
{
    ps3_cue_unload();

    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        ps3_log("Failed to open CD image: %s\n", img_path);
        return -1;
    }
    setvbuf(fp, NULL, _IOFBF, 32768);

    uint64_t fsize = get_file_size_u64(img_path, fp);
    if (fsize < 2048ULL) {
        fclose(fp);
        return -1;
    }

    uint8_t probe[4704];
    fseek(fp, 0, SEEK_SET);
    size_t got = fread(probe, 1, sizeof(probe), fp);

    if (got >= 16 && is_raw_mode_sync(probe)) {
        g_num_tracks = 1;
        g_tracks[1].fp = fp;
        snprintf(g_tracks[1].bin_path, sizeof(g_tracks[1].bin_path), "%s", img_path);
        g_tracks[1].is_audio = 0;
        g_tracks[1].sector_size = 2352;
        g_tracks[1].data_offset = (probe[15] == 2) ? 24 : 16;
        g_tracks[1].start_lba = 0;
        g_tracks[1].length_lba = (uint32_t)(fsize / 2352ULL);
        g_tracks[1].file_byte_offset = 0;
        g_tracks[1].file_data_base = 0;
        g_tracks[1].file_data_size = fsize;
    } else if ((fsize % 2048ULL) == 0ULL && (fsize % 2352ULL) != 0ULL) {
        g_num_tracks = 1;
        g_tracks[1].fp = fp;
        snprintf(g_tracks[1].bin_path, sizeof(g_tracks[1].bin_path), "%s", img_path);
        g_tracks[1].is_audio = 0;
        g_tracks[1].sector_size = 2048;
        g_tracks[1].data_offset = 0;
        g_tracks[1].start_lba = 0;
        g_tracks[1].length_lba = (uint32_t)(fsize / 2048ULL);
        g_tracks[1].file_byte_offset = 0;
        g_tracks[1].file_data_base = 0;
        g_tracks[1].file_data_size = fsize;
    } else if ((fsize % 2352ULL) == 0ULL) {
        uint32_t total_secs = (uint32_t)(fsize / 2352ULL);
        uint32_t scan_limit = (total_secs < 15000u) ? total_secs : 15000u;
        uint32_t found_sec = 0;
        int found = 0;
        uint8_t found_hdr[16];
        memset(found_hdr, 0, sizeof(found_hdr));

        uint8_t *chunk = (uint8_t *)malloc(64u * 2352u);
        if (chunk) {
            for (uint32_t base = 1; base + 1 < scan_limit; base += 63u) {
                uint32_t count = scan_limit - base;
                if (count > 64u) count = 64u;
                fseek(fp, (long)((uint64_t)base * 2352ULL), SEEK_SET);
                size_t rd = fread(chunk, 2352u, count, fp);
                if (rd < 2) break;
                for (size_t j = 0; j + 1 < rd; j++) {
                    const uint8_t *s0 = chunk + j * 2352u;
                    const uint8_t *s1 = chunk + (j + 1) * 2352u;
                    if (is_raw_mode_sync(s0) && is_raw_mode_sync(s1)) {
                        found_sec = base + (uint32_t)j;
                        memcpy(found_hdr, s0, 16);
                        found = 1;
                        break;
                    }
                }
                if (found) break;
            }
            free(chunk);
        }

        if (found && found_sec > 0) {
            uint32_t data_lba = found_sec + 225u;
            if (bcd_byte_valid(found_hdr[12]) &&
                bcd_byte_valid(found_hdr[13]) &&
                bcd_byte_valid(found_hdr[14])) {
                uint32_t cand = msf_to_lba_clamped(
                    (int)bcd_byte_to_bin(found_hdr[12]),
                    (int)bcd_byte_to_bin(found_hdr[13]),
                    (int)bcd_byte_to_bin(found_hdr[14]));
                if (cand >= found_sec && cand <= found_sec + 1500u) {
                    data_lba = cand;
                }
            }

            g_num_tracks = 2;
            g_tracks[1].fp = fp;
            snprintf(g_tracks[1].bin_path, sizeof(g_tracks[1].bin_path), "%s", img_path);
            g_tracks[1].is_audio = 1;
            g_tracks[1].sector_size = 2352;
            g_tracks[1].data_offset = 0;
            g_tracks[1].start_lba = 0;
            g_tracks[1].length_lba = found_sec;
            g_tracks[1].file_byte_offset = 0;
            g_tracks[1].file_data_base = 0;
            g_tracks[1].file_data_size = fsize;

            g_tracks[2].fp = fp;
            snprintf(g_tracks[2].bin_path, sizeof(g_tracks[2].bin_path), "%s", img_path);
            g_tracks[2].is_audio = 0;
            g_tracks[2].sector_size = 2352;
            g_tracks[2].data_offset = (found_hdr[15] == 2) ? 24 : 16;
            g_tracks[2].start_lba = data_lba;
            g_tracks[2].length_lba = total_secs - found_sec;
            g_tracks[2].file_byte_offset = (uint64_t)found_sec * 2352ULL;
            g_tracks[2].file_data_base = 0;
            g_tracks[2].file_data_size = fsize;
        } else {
            g_num_tracks = 1;
            g_tracks[1].fp = fp;
            snprintf(g_tracks[1].bin_path, sizeof(g_tracks[1].bin_path), "%s", img_path);
            g_tracks[1].is_audio = 0;
            g_tracks[1].sector_size = 2352;
            g_tracks[1].data_offset = 16;
            g_tracks[1].start_lba = 0;
            g_tracks[1].length_lba = total_secs;
            g_tracks[1].file_byte_offset = 0;
            g_tracks[1].file_data_base = 0;
            g_tracks[1].file_data_size = fsize;
        }
    } else {
        g_num_tracks = 1;
        g_tracks[1].fp = fp;
        snprintf(g_tracks[1].bin_path, sizeof(g_tracks[1].bin_path), "%s", img_path);
        g_tracks[1].is_audio = 0;
        g_tracks[1].sector_size = 2048;
        g_tracks[1].data_offset = 0;
        g_tracks[1].start_lba = 0;
        g_tracks[1].length_lba = (uint32_t)(fsize / 2048ULL);
        g_tracks[1].file_byte_offset = 0;
        g_tracks[1].file_data_base = 0;
        g_tracks[1].file_data_size = fsize;
    }

    cd_cache_invalidate();
    ensure_cdda_thread();
    populate_hugo_toc();
    g_cue_loaded = 1;
    ps3_log("CD image opened directly: %s (%d tracks, sector=%u)\n",
            img_path, g_num_tracks, g_tracks[g_num_tracks].sector_size);
    return 0;
}

static int find_track_for_lba(uint32_t lba)
{
    if (g_last_data_trk >= 1 && g_last_data_trk <= g_num_tracks) {
        ps3_cd_track_t *ht = &g_tracks[g_last_data_trk];
        if (lba >= ht->start_lba && lba < ht->start_lba + ht->length_lba) {
            return g_last_data_trk;
        }
    }
    for (int i = 1; i <= g_num_tracks; i++) {
        if (lba >= g_tracks[i].start_lba &&
            lba < g_tracks[i].start_lba + g_tracks[i].length_lba) {
            g_last_data_trk = i;
            return i;
        }
    }
    return 0;
}

static void seek_data_fp_if_needed(FILE *fp, uint64_t pos)
{
    if (g_last_data_fp != fp || g_last_data_pos != pos) {
        fseek(fp, (long)pos, SEEK_SET);
    }
}

static void read_sectors_impl(unsigned char *buf, uint32_t lba, uint32_t count)
{
    if (!buf || count == 0) return;
    if (!g_cue_loaded) {
        memset(buf, 0, (size_t)count * 2048u);
        return;
    }

    uint32_t done = 0;
    while (done < count) {
        uint32_t cur_lba = lba + done;
        int t = find_track_for_lba(cur_lba);
        ps3_cd_track_t *tr = (t > 0) ? &g_tracks[t] : NULL;
        if (!tr || !tr->fp) {
            memset(buf + (size_t)done * 2048u, 0, 2048);
            done++;
            continue;
        }

        uint32_t rel = cur_lba - tr->start_lba;
        uint32_t avail = tr->length_lba - rel;
        uint32_t batch = count - done;
        if (batch > avail) batch = avail;
        if (batch == 0) batch = 1;

        if (batch > 1 && tr->sector_size == 2048 && tr->data_offset == 0) {
            uint64_t pos = tr->file_byte_offset + (uint64_t)rel * 2048ULL;
            seek_data_fp_if_needed(tr->fp, pos);
            size_t rd = fread(buf + (size_t)done * 2048u, 2048, batch, tr->fp);
            if (rd < batch) {
                memset(buf + (size_t)(done + rd) * 2048u, 0, (size_t)(batch - rd) * 2048u);
                g_last_data_fp = NULL;
                g_last_data_pos = (uint64_t)-1;
            } else {
                g_last_data_fp = tr->fp;
                g_last_data_pos = pos + (uint64_t)rd * 2048ULL;
            }
            done += batch;
        } else if (batch > 1 && tr->sector_size == 2352 && tr->data_offset + 2048u <= 2352u) {
            if (batch > CD_CACHE_BLK_SECTORS) batch = CD_CACHE_BLK_SECTORS;
            uint64_t raw_pos = tr->file_byte_offset + (uint64_t)rel * 2352ULL;
            seek_data_fp_if_needed(tr->fp, raw_pos);
            size_t rd = fread(g_cd_batch_raw, 2352, batch, tr->fp);
            for (size_t i = 0; i < rd; i++) {
                memcpy(buf + (size_t)(done + i) * 2048u,
                       g_cd_batch_raw + i * 2352u + tr->data_offset, 2048);
            }
            if (rd < batch) {
                memset(buf + (size_t)(done + rd) * 2048u, 0, (size_t)(batch - rd) * 2048u);
                g_last_data_fp = NULL;
                g_last_data_pos = (uint64_t)-1;
            } else {
                g_last_data_fp = tr->fp;
                g_last_data_pos = raw_pos + (uint64_t)rd * 2352ULL;
            }
            done += batch;
        } else {
            uint64_t pos = tr->file_byte_offset + (uint64_t)rel * tr->sector_size + tr->data_offset;
            seek_data_fp_if_needed(tr->fp, pos);
            if (fread(buf + (size_t)done * 2048u, 1, 2048, tr->fp) != 2048) {
                memset(buf + (size_t)done * 2048u, 0, 2048);
                g_last_data_fp = NULL;
                g_last_data_pos = (uint64_t)-1;
            } else {
                g_last_data_fp = tr->fp;
                g_last_data_pos = pos + 2048ULL;
            }
            done++;
        }
    }
}

static const uint8_t *cd_cache_get_block(uint32_t blk_lba)
{
    if (g_cd_cache_last >= 0 && (uint32_t)g_cd_cache_last < CD_CACHE_SLOTS &&
        g_cd_cache[g_cd_cache_last].valid &&
        g_cd_cache[g_cd_cache_last].base_lba == blk_lba) {
        g_cd_cache[g_cd_cache_last].age = g_cd_cache_tick++;
        if (g_cd_cache_tick == 0u) {
            for (uint32_t j = 0; j < CD_CACHE_SLOTS; j++) {
                g_cd_cache[j].age = j + 1u;
            }
            g_cd_cache_tick = CD_CACHE_SLOTS + 1u;
        }
        return g_cd_cache[g_cd_cache_last].data;
    }

    int empty_idx = -1;
    int oldest_idx = 0;
    uint32_t oldest_age = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < CD_CACHE_SLOTS; i++) {
        if (g_cd_cache[i].valid) {
            if (g_cd_cache[i].base_lba == blk_lba) {
                g_cd_cache[i].age = g_cd_cache_tick++;
                if (g_cd_cache_tick == 0u) {
                    for (uint32_t j = 0; j < CD_CACHE_SLOTS; j++) {
                        g_cd_cache[j].age = j + 1u;
                    }
                    g_cd_cache_tick = CD_CACHE_SLOTS + 1u;
                }
                g_cd_cache_last = (int)i;
                return g_cd_cache[i].data;
            }
            if (g_cd_cache[i].age < oldest_age) {
                oldest_age = g_cd_cache[i].age;
                oldest_idx = (int)i;
            }
        } else if (empty_idx < 0) {
            empty_idx = (int)i;
        }
    }

    int victim = (empty_idx >= 0) ? empty_idx : oldest_idx;
    read_sectors_impl(g_cd_cache[victim].data, blk_lba, CD_CACHE_BLK_SECTORS);
    g_cd_cache[victim].base_lba = blk_lba;
    g_cd_cache[victim].valid = 1;
    g_cd_cache[victim].age = g_cd_cache_tick++;
    if (g_cd_cache_tick == 0u) {
        for (uint32_t j = 0; j < CD_CACHE_SLOTS; j++) {
            g_cd_cache[j].age = j + 1u;
        }
        g_cd_cache_tick = CD_CACHE_SLOTS + 1u;
    }
    g_cd_cache_last = victim;
    return g_cd_cache[victim].data;
}

void ps3_cue_read_sectors(unsigned char *buf, uint32_t lba, uint32_t count)
{
    if (!buf || count == 0) return;
    if (!g_cue_loaded) {
        memset(buf, 0, (size_t)count * 2048u);
        return;
    }

    uint32_t done = 0;
    while (done < count) {
        uint32_t cur = lba + done;
        uint32_t blk = cur & ~(CD_CACHE_BLK_SECTORS - 1u);
        uint32_t off = cur - blk;
        uint32_t take = CD_CACHE_BLK_SECTORS - off;
        if (take > count - done) take = count - done;
        const uint8_t *blk_data = cd_cache_get_block(blk);
        memcpy(buf + (size_t)done * 2048u, blk_data + (size_t)off * 2048u, (size_t)take * 2048u);
        done += take;
    }
}

void ps3_cue_read_sector(unsigned char *buf, uint32_t lba)
{
    ps3_cue_read_sectors(buf, lba, 1);
}

static void cdda_thread_entry(void *arg)
{
    (void)arg;
    FILE *fp = NULL;
    char fp_path[512] = "";
    uint64_t file_pos = (uint64_t)-1;
    cdda_cmd_t cur;
    memset(&cur, 0, sizeof(cur));
    uint32_t applied_seq = 0;
    uint64_t worker_pos = 0;
    int worker_eof = 0;
    uint8_t raw_chunk[CDDA_CHUNK_FRAMES * 4u];

    for (;;) {
        uint32_t s1 = g_cdda_cmd_seq;
        __sync_synchronize();
        if ((s1 & 1u) != 0u) {
            sysUsleep(20);
            continue;
        }
        if (s1 != applied_seq) {
            cdda_cmd_t next_cmd = g_cdda_cmd;
            __sync_synchronize();
            uint32_t s2 = g_cdda_cmd_seq;
            if (s1 != s2) {
                continue;
            }
            applied_seq = s1;
            cur = next_cmd;
            worker_eof = 0;
            g_cdda_eof_gen = 0;
            g_cdda_wr = 0;
            g_cdda_rd = 0;
            if (!cur.active || cur.bin_path[0] == '\0') {
                if (fp) {
                    fclose(fp);
                    fp = NULL;
                }
                fp_path[0] = '\0';
                file_pos = (uint64_t)-1;
                worker_pos = 0;
            } else {
                if (fp && strcmp(fp_path, cur.bin_path) != 0) {
                    fclose(fp);
                    fp = NULL;
                    fp_path[0] = '\0';
                    file_pos = (uint64_t)-1;
                }
                if (!fp) {
                    fp = fopen(cur.bin_path, "rb");
                    if (fp) {
                        setvbuf(fp, NULL, _IOFBF, 32768);
                        snprintf(fp_path, sizeof(fp_path), "%s", cur.bin_path);
                        file_pos = (uint64_t)-1;
                    }
                }
                worker_pos = cur.byte_start;
            }
            __sync_synchronize();
            g_cdda_ring_gen = cur.gen;
            g_cdda_ack_gen  = cur.gen;
            continue;
        }

        if (cur.active && !worker_eof && fp && !g_cdda_paused) {
            uint32_t rd = g_cdda_rd;
            uint32_t wr = g_cdda_wr;
            uint32_t used = wr - rd;
            uint32_t free_space = CDDA_RING_FRAMES - used;
            if (free_space >= CDDA_CHUNK_FRAMES) {
                uint64_t avail_bytes = (worker_pos < cur.byte_end) ? (cur.byte_end - worker_pos) : 0ULL;
                uint64_t avail_frames = avail_bytes / 4ULL;
                if (avail_frames == 0ULL) {
                    if (cur.repeat && cur.byte_end > cur.byte_start) {
                        worker_pos = cur.byte_start;
                        continue;
                    } else {
                        worker_eof = 1;
                        __sync_synchronize();
                        g_cdda_eof_gen = cur.gen;
                        continue;
                    }
                }
                uint32_t want_frames = CDDA_CHUNK_FRAMES;
                if ((uint64_t)want_frames > avail_frames) {
                    want_frames = (uint32_t)avail_frames;
                }
                if (file_pos != worker_pos) {
                    fseek(fp, (long)worker_pos, SEEK_SET);
                }
                size_t got = fread(raw_chunk, 4, want_frames, fp);
                if (got > 0) {
                    file_pos = worker_pos + (uint64_t)got * 4ULL;
                    worker_pos = file_pos;
                    for (size_t k = 0; k < got; k++) {
                        uint32_t idx = (wr + (uint32_t)k) & CDDA_RING_MASK;
                        uint16_t lo_l = raw_chunk[k * 4u + 0u];
                        uint16_t hi_l = raw_chunk[k * 4u + 1u];
                        uint16_t lo_r = raw_chunk[k * 4u + 2u];
                        uint16_t hi_r = raw_chunk[k * 4u + 3u];
                        g_cdda_ring[idx * 2u + 0u] = (int16_t)(uint16_t)(lo_l | (hi_l << 8));
                        g_cdda_ring[idx * 2u + 1u] = (int16_t)(uint16_t)(lo_r | (hi_r << 8));
                    }
                    __sync_synchronize();
                    g_cdda_wr = wr + (uint32_t)got;
                    continue;
                } else {
                    file_pos = (uint64_t)-1;
                    if (cur.repeat && cur.byte_end > cur.byte_start && worker_pos > cur.byte_start) {
                        worker_pos = cur.byte_start;
                    } else {
                        worker_eof = 1;
                        __sync_synchronize();
                        g_cdda_eof_gen = cur.gen;
                    }
                }
            }
            sysUsleep(2000);
        } else {
            sysUsleep(5000);
        }
    }
    sysThreadExit(0);
}

static void ensure_cdda_thread(void)
{
    if (g_cdda_thread_started) return;
    if (sysThreadCreate(&g_cdda_tid, cdda_thread_entry, NULL, 1100, 0x8000,
                        THREAD_JOINABLE, "hugo_cdda") == 0) {
        g_cdda_thread_started = 1;
    }
}

static uint32_t cdda_send_cmd(int active, int repeat, int track,
                              uint64_t byte_start, uint64_t byte_end,
                              const char *bin_path)
{
    uint32_t gen = g_cdda_active_gen + 1u;
    if (gen == 0u) gen = 1u;
    g_cdda_active_gen = gen;
    __sync_synchronize();
    __sync_fetch_and_add(&g_cdda_cmd_seq, 1u);
    __sync_synchronize();
    g_cdda_cmd.gen        = gen;
    g_cdda_cmd.active     = active;
    g_cdda_cmd.repeat     = repeat;
    g_cdda_cmd.track      = track;
    g_cdda_cmd.byte_start = byte_start;
    g_cdda_cmd.byte_end   = byte_end;
    if (bin_path && bin_path[0]) {
        snprintf(g_cdda_cmd.bin_path, sizeof(g_cdda_cmd.bin_path), "%s", bin_path);
    } else {
        g_cdda_cmd.bin_path[0] = '\0';
    }
    __sync_synchronize();
    __sync_fetch_and_add(&g_cdda_cmd_seq, 1u);
    return gen;
}

int ps3_cue_play_cdda_track(unsigned char track, int repeat)
{
    if (!g_cue_loaded || track < 1 || track > g_num_tracks) return -1;
    ps3_cd_track_t *tr = &g_tracks[track];
    if (!tr->is_audio || !tr->fp || !tr->bin_path[0]) return -1;

    ensure_cdda_thread();
    if (!g_cdda_thread_started) return -1;

    uint64_t b_start = tr->file_byte_offset;
    uint64_t b_end   = tr->file_byte_offset + (uint64_t)tr->length_lba * 2352ULL;

    g_cdda_track        = track;
    g_cdda_paused       = 0;
    g_cdda_byte_start   = b_start;
    g_cdda_byte_end     = b_end;
    g_cdda_played_bytes = 0;
    g_cdda_phase_160    = 0;
    g_cdda_playing      = 1;

    cdda_send_cmd(1, repeat, track, b_start, b_end, tr->bin_path);
    return 0;
}

int ps3_cue_play_cdda_range(int m1, int s1, int f1, int m2, int s2, int f2, int repeat)
{
    if (!g_cue_loaded || g_num_tracks < 1) return -1;

    uint32_t start_lba = msf_to_lba_clamped(m1, s1, f1);
    uint32_t end_lba   = msf_to_lba_clamped(m2, s2, f2);

    int t = 0;
    for (int i = 1; i <= g_num_tracks; i++) {
        if (!g_tracks[i].is_audio) continue;
        uint32_t t_end = g_tracks[i].start_lba + g_tracks[i].length_lba;
        if (start_lba >= g_tracks[i].start_lba && start_lba < t_end) {
            t = i;
            break;
        }
    }
    if (t == 0) {
        for (int i = 1; i <= g_num_tracks; i++) {
            if (!g_tracks[i].is_audio) continue;
            if (start_lba <= g_tracks[i].start_lba + 225u) {
                t = i;
                start_lba = g_tracks[i].start_lba;
                break;
            }
        }
    }
    if (t == 0) return -1;

    ps3_cd_track_t *tr = &g_tracks[t];
    if (!tr->fp || !tr->bin_path[0]) return -1;

    ensure_cdda_thread();
    if (!g_cdda_thread_started) return -1;

    uint32_t rel_start = (start_lba > tr->start_lba) ? (start_lba - tr->start_lba) : 0u;
    uint32_t rel_end = tr->length_lba;
    if (end_lba > start_lba) {
        uint32_t cand_end = end_lba - tr->start_lba;
        if (cand_end < rel_end) rel_end = cand_end;
    }
    if (rel_end <= rel_start) rel_end = tr->length_lba;

    uint64_t b_start = tr->file_byte_offset + (uint64_t)rel_start * 2352ULL;
    uint64_t b_end   = tr->file_byte_offset + (uint64_t)rel_end * 2352ULL;

    g_cdda_track        = t;
    g_cdda_paused       = 0;
    g_cdda_byte_start   = b_start;
    g_cdda_byte_end     = b_end;
    g_cdda_played_bytes = 0;
    g_cdda_phase_160    = 0;
    g_cdda_playing      = 1;

    cdda_send_cmd(1, repeat, t, b_start, b_end, tr->bin_path);
    return 0;
}

void ps3_cue_stop_cdda(void)
{
    g_cdda_playing = 0;
    g_cdda_paused  = 0;
    if (g_cdda_thread_started) {
        cdda_send_cmd(0, 0, 0, 0ULL, 0ULL, "");
    }
}

void ps3_cue_pause_cdda(int pause)
{
    if (!g_cdda_playing) return;
    g_cdda_paused = pause ? 1 : 0;
}

int ps3_cue_get_cdda_status(void)
{
    if (!g_cdda_playing) return 0;
    return g_cdda_paused ? CDROM_AUDIO_PAUSED : CDROM_AUDIO_PLAY;
}

void ps3_cue_get_disc_length(int *m, int *s, int *f)
{
    uint32_t end_lba = 0;
    if (g_cue_loaded && g_num_tracks >= 1) {
        ps3_cd_track_t *last = &g_tracks[g_num_tracks];
        end_lba = last->start_lba + last->length_lba;
    }
    UChar om = 60, os = 0, of = 0;
    if (end_lba > 0) {
        lba_to_msf(end_lba, &om, &os, &of);
    }
    if (m) *m = (int)om;
    if (s) *s = (int)os;
    if (f) *f = (int)of;
}

void ps3_cue_get_subchannel_info(uint8_t out_subq[10])
{
    memset(out_subq, 0, 10);
    if (!g_cue_loaded || g_num_tracks < 1) {
        out_subq[0] = 3;
        return;
    }

    int trk = (g_cdda_track >= 1 && g_cdda_track <= g_num_tracks) ? g_cdda_track : 1;
    ps3_cd_track_t *tr = &g_tracks[trk];
    uint32_t rel_lba = 0;
    if (g_cdda_playing && g_cdda_byte_end > g_cdda_byte_start) {
        uint64_t span = g_cdda_byte_end - g_cdda_byte_start;
        uint64_t pos_off = g_cdda_played_bytes;
        if (span > 0ULL && pos_off >= span) {
            pos_off %= span;
        }
        uint64_t track_off = (g_cdda_byte_start >= tr->file_byte_offset)
            ? (g_cdda_byte_start - tr->file_byte_offset)
            : 0ULL;
        rel_lba = (uint32_t)((track_off + pos_off) / 2352ULL);
    }
    uint32_t abs_lba = tr->start_lba + rel_lba;

    UChar rm, rs, rf, am, as, af;
    uint32_t rm32 = rel_lba / (75u * 60u);
    if (rm32 > 99u) rm32 = 99u;
    rm = (UChar)rm32;
    rs = (UChar)((rel_lba / 75u) % 60u);
    rf = (UChar)(rel_lba % 75u);
    lba_to_msf(abs_lba, &am, &as, &af);

    out_subq[0] = g_cdda_playing ? (g_cdda_paused ? 1 : 0) : 3;
    out_subq[1] = tr->is_audio ? 0 : 4;
    out_subq[2] = binbcd[trk];
    out_subq[3] = 0x01;
    out_subq[4] = binbcd[rm];
    out_subq[5] = binbcd[rs];
    out_subq[6] = binbcd[rf];
    out_subq[7] = binbcd[am];
    out_subq[8] = binbcd[as];
    out_subq[9] = binbcd[af];
}

void ps3_cue_mix_cdda(int16_t *stereo_accum, size_t frames)
{
    static int16_t  s_prev_l = 0;
    static int16_t  s_prev_r = 0;
    static uint32_t s_last_gen = 0;

    if (!g_cdda_playing || g_cdda_paused ||
        g_cdda_track < 1 || g_cdda_track > g_num_tracks) {
        return;
    }

    uint32_t agen = g_cdda_active_gen;
    if (g_cdda_ring_gen != agen) {
        s_prev_l = 0;
        s_prev_r = 0;
        s_last_gen = agen;
        return;
    }
    if (s_last_gen != agen) {
        s_prev_l = 0;
        s_prev_r = 0;
        s_last_gen = agen;
    }

    __sync_synchronize();
    uint32_t rd = g_cdda_rd;
    uint32_t wr = g_cdda_wr;
    uint32_t avail = wr - rd;

    for (size_t i = 0; i < frames; i++) {
        if (avail == 0u) {
            wr = g_cdda_wr;
            avail = wr - rd;
            if (avail == 0u) {
                if (g_cdda_eof_gen == agen) {
                    g_cdda_playing = 0;
                }
                break;
            }
        }

        uint32_t idx = rd & CDDA_RING_MASK;
        int32_t cur_l = g_cdda_ring[idx * 2u + 0u];
        int32_t cur_r = g_cdda_ring[idx * 2u + 1u];
        int32_t w1 = (int32_t)g_cdda_phase_160;
        int32_t w0 = 160 - w1;
        int32_t out_l = ((int32_t)s_prev_l * w0 + cur_l * w1) / 160;
        int32_t out_r = ((int32_t)s_prev_r * w0 + cur_r * w1) / 160;

        stereo_accum[i * 2u + 0u] = (int16_t)((out_l * 3) >> 2);
        stereo_accum[i * 2u + 1u] = (int16_t)((out_r * 3) >> 2);

        g_cdda_phase_160 += 147u;
        if (g_cdda_phase_160 >= 160u) {
            g_cdda_phase_160 -= 160u;
            s_prev_l = (int16_t)cur_l;
            s_prev_r = (int16_t)cur_r;
            rd++;
            avail--;
            g_cdda_played_bytes += 4ULL;
        }
    }

    __sync_synchronize();
    g_cdda_rd = rd;
}
