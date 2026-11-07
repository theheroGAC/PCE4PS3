#include "ps3_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "pce.h"
#include "cd.h"

typedef struct {
    FILE    *fp;
    char     bin_path[512];
    int      is_audio;
    uint32_t sector_size;
    uint32_t data_offset;
    uint32_t start_lba;
    uint32_t length_lba;
    uint64_t file_byte_offset;
} ps3_cd_track_t;

static ps3_cd_track_t g_tracks[100];
static int            g_num_tracks = 0;
static int            g_cue_loaded = 0;

static int      g_cdda_playing = 0;
static int      g_cdda_repeat  = 0;
static int      g_cdda_track   = 0;
static uint64_t g_cdda_byte_pos = 0;
static uint64_t g_cdda_byte_end = 0;
static uint32_t g_cdda_frac_fp  = 0;

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
    uint32_t total = lba + 150;
    *m = (UChar)(total / (75 * 60));
    *s = (UChar)((total / 75) % 60);
    *f = (UChar)(total % 75);
}

void ps3_cue_unload(void)
{
    g_cdda_playing = 0;
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
    int cur_track = 0;
    uint32_t running_lba = 0;

    char line[512];
    while (fgets(line, sizeof(line), cue)) {
        trim_line(line);
        char *p = skip_ws(line);
        if (!*p) continue;

        if (!strncasecmp(p, "FILE", 4)) {
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
            snprintf(cur_file_path, sizeof(cur_file_path), "%s/%s", base_dir, fname);
            cur_fp = fopen(cur_file_path, "rb");
            if (!cur_fp) {
                ps3_log("CUE referenced missing file: %s\n", cur_file_path);
            }
        } else if (!strncasecmp(p, "TRACK", 5)) {
            int tnum = 0;
            char mode[64] = "";
            if (sscanf(p + 5, "%d %63s", &tnum, mode) == 2 && tnum >= 1 && tnum < 100) {
                cur_track = tnum;
                if (tnum > g_num_tracks) g_num_tracks = tnum;
                ps3_cd_track_t *tr = &g_tracks[cur_track];
                tr->fp = cur_fp;
                snprintf(tr->bin_path, sizeof(tr->bin_path), "%s", cur_file_path);
                if (!strcasecmp(mode, "AUDIO")) {
                    tr->is_audio    = 1;
                    tr->sector_size = 2352;
                    tr->data_offset = 0;
                } else if (strstr(mode, "2352")) {
                    tr->is_audio    = 0;
                    tr->sector_size = 2352;
                    tr->data_offset = 16;
                } else {
                    tr->is_audio    = 0;
                    tr->sector_size = 2048;
                    tr->data_offset = 0;
                }
            }
        } else if (!strncasecmp(p, "INDEX", 5) && cur_track >= 1) {
            int idx = 0, mm = 0, ss = 0, ff = 0;
            if (sscanf(p + 5, "%d %d:%d:%d", &idx, &mm, &ss, &ff) == 4 && idx == 1) {
                uint32_t idx_lba = (uint32_t)(mm * 60 * 75 + ss * 75 + ff);
                ps3_cd_track_t *tr = &g_tracks[cur_track];
                if (cur_track > 1 && strcmp(tr->bin_path, g_tracks[cur_track - 1].bin_path) == 0) {
                    tr->start_lba = idx_lba;
                    tr->file_byte_offset = (uint64_t)idx_lba * tr->sector_size;
                    g_tracks[cur_track - 1].length_lba =
                        idx_lba - g_tracks[cur_track - 1].start_lba;
                } else {
                    if (cur_track > 1 && g_tracks[cur_track - 1].fp) {
                        FILE *pf = g_tracks[cur_track - 1].fp;
                        fseek(pf, 0, SEEK_END);
                        long sz = ftell(pf);
                        uint32_t secs = (uint32_t)(sz / (long)g_tracks[cur_track - 1].sector_size);
                        g_tracks[cur_track - 1].length_lba = secs;
                        running_lba = g_tracks[cur_track - 1].start_lba + secs;
                    }
                    tr->start_lba = running_lba;
                    tr->file_byte_offset = (uint64_t)idx_lba * tr->sector_size;
                }
            }
        }
    }
    fclose(cue);

    if (g_num_tracks < 1) return -1;

    ps3_cd_track_t *last = &g_tracks[g_num_tracks];
    if (last->fp) {
        fseek(last->fp, 0, SEEK_END);
        long sz = ftell(last->fp);
        if (sz > (long)last->file_byte_offset) {
            last->length_lba = (uint32_t)((sz - (long)last->file_byte_offset) / last->sector_size);
        }
    }

    nb_max_track = (UChar)g_num_tracks;
    for (int i = 1; i <= g_num_tracks; i++) {
        ps3_cd_track_t *tr = &g_tracks[i];
        lba_to_msf(tr->start_lba, &CD_track[i].beg_min, &CD_track[i].beg_sec, &CD_track[i].beg_fra);
        CD_track[i].type       = tr->is_audio ? 0 : 4;
        CD_track[i].beg_lsn    = tr->start_lba;
        CD_track[i].length     = tr->length_lba;
        CD_track[i].source_type = 0;
    }

    g_cue_loaded = 1;
    ps3_log("CUE loaded: %s (%d tracks)\n", cue_path, g_num_tracks);
    return 0;
}

void ps3_cue_read_sector(unsigned char *buf, uint32_t lba)
{
    if (!g_cue_loaded) {
        memset(buf, 0, 2048);
        return;
    }
    int t = 1;
    for (int i = 1; i <= g_num_tracks; i++) {
        if (lba >= g_tracks[i].start_lba &&
            lba < g_tracks[i].start_lba + g_tracks[i].length_lba) {
            t = i;
            break;
        }
    }
    ps3_cd_track_t *tr = &g_tracks[t];
    if (!tr->fp) {
        memset(buf, 0, 2048);
        return;
    }
    uint32_t rel = (lba >= tr->start_lba) ? (lba - tr->start_lba) : 0;
    uint64_t pos = tr->file_byte_offset + (uint64_t)rel * tr->sector_size + tr->data_offset;
    fseek(tr->fp, (long)pos, SEEK_SET);
    if (fread(buf, 1, 2048, tr->fp) != 2048) {
        memset(buf, 0, 2048);
    }
}

int ps3_cue_play_cdda_track(unsigned char track, int repeat)
{
    if (!g_cue_loaded || track < 1 || track > g_num_tracks) return -1;
    ps3_cd_track_t *tr = &g_tracks[track];
    if (!tr->is_audio || !tr->fp) return -1;

    g_cdda_track    = track;
    g_cdda_repeat   = repeat;
    g_cdda_byte_pos = tr->file_byte_offset;
    g_cdda_byte_end = tr->file_byte_offset + (uint64_t)tr->length_lba * 2352ULL;
    g_cdda_frac_fp  = 0;
    g_cdda_playing  = 1;
    return 0;
}

void ps3_cue_stop_cdda(void)
{
    g_cdda_playing = 0;
}

void ps3_cue_mix_cdda(int16_t *stereo_accum, size_t frames)
{
    if (!g_cdda_playing || g_cdda_track < 1 || g_cdda_track > g_num_tracks) return;
    ps3_cd_track_t *tr = &g_tracks[g_cdda_track];
    if (!tr->fp) return;

    const uint32_t step_fp = (44100u << 16) / PS3_AUDIO_RATE;

    uint8_t raw[4];
    for (size_t i = 0; i < frames; i++) {
        g_cdda_frac_fp += step_fp;
        if (g_cdda_frac_fp >= 0x10000u) {
            g_cdda_frac_fp -= 0x10000u;
            g_cdda_byte_pos += 4;
            if (g_cdda_byte_pos >= g_cdda_byte_end) {
                if (g_cdda_repeat) {
                    g_cdda_byte_pos = tr->file_byte_offset;
                } else {
                    g_cdda_playing = 0;
                    return;
                }
            }
        }
        fseek(tr->fp, (long)g_cdda_byte_pos, SEEK_SET);
        if (fread(raw, 1, 4, tr->fp) == 4) {
            int16_t l = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
            int16_t r = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
            stereo_accum[i * 2 + 0] = l;
            stereo_accum[i * 2 + 1] = r;
        }
    }
}
