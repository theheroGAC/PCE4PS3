#include "ps3_platform.h"

#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int is_pce_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return (!strcasecmp(dot, ".pce") ||
            !strcasecmp(dot, ".sgx") ||
            !strcasecmp(dot, ".bin") ||
            !strcasecmp(dot, ".rom"));
}

static char g_zip_entry_name[256];

char *find_possible_filename_in_zip(char *zipfilename)
{
    g_zip_entry_name[0] = '\0';
    FILE *fp = fopen(zipfilename, "rb");
    if (!fp) return g_zip_entry_name;

    uint8_t hdr[30];
    while (fread(hdr, 1, 30, fp) == 30) {
        if (rd_le32(hdr) != 0x04034b50u) break;
        uint16_t flags     = rd_le16(hdr + 6);
        uint32_t comp_size = rd_le32(hdr + 18);
        uint16_t name_len  = rd_le16(hdr + 26);
        uint16_t extra_len = rd_le16(hdr + 28);

        char name[256];
        size_t to_read = name_len < 255 ? name_len : 255;
        if (fread(name, 1, to_read, fp) != to_read) break;
        name[to_read] = '\0';
        if (name_len > to_read) fseek(fp, name_len - to_read, SEEK_CUR);
        fseek(fp, extra_len, SEEK_CUR);

        if (is_pce_ext(name)) {
            snprintf(g_zip_entry_name, sizeof(g_zip_entry_name), "%s", name);
            break;
        }

        if (flags & 0x08) break;
        fseek(fp, comp_size, SEEK_CUR);
    }

    fclose(fp);
    return g_zip_entry_name;
}

unsigned char *extract_file_in_memory(char *zipfilename,
                                      char *archivedfile,
                                      size_t *filesize)
{
    if (filesize) *filesize = 0;
    FILE *fp = fopen(zipfilename, "rb");
    if (!fp) return NULL;

    uint8_t hdr[30];
    unsigned char *result = NULL;

    while (fread(hdr, 1, 30, fp) == 30) {
        if (rd_le32(hdr) != 0x04034b50u) break;
        uint16_t method     = rd_le16(hdr + 8);
        uint32_t comp_size  = rd_le32(hdr + 18);
        uint32_t uncomp_sz  = rd_le32(hdr + 22);
        uint16_t name_len   = rd_le16(hdr + 26);
        uint16_t extra_len  = rd_le16(hdr + 28);

        char name[256];
        size_t to_read = name_len < 255 ? name_len : 255;
        if (fread(name, 1, to_read, fp) != to_read) break;
        name[to_read] = '\0';
        if (name_len > to_read) fseek(fp, name_len - to_read, SEEK_CUR);
        fseek(fp, extra_len, SEEK_CUR);

        if (strcasecmp(name, archivedfile) == 0 && uncomp_sz > 0 && uncomp_sz <= (16 * 1024 * 1024)) {
            if (method == 0) {
                result = (unsigned char *)malloc(uncomp_sz);
                if (result && fread(result, 1, uncomp_sz, fp) == uncomp_sz) {
                    if (filesize) *filesize = uncomp_sz;
                } else {
                    free(result);
                    result = NULL;
                }
            } else if (method == 8) {
                uint8_t *comp = (uint8_t *)malloc(comp_size);
                result = (unsigned char *)malloc(uncomp_sz);
                if (comp && result && fread(comp, 1, comp_size, fp) == comp_size) {
                    z_stream zs;
                    memset(&zs, 0, sizeof(zs));
                    zs.next_in   = comp;
                    zs.avail_in  = comp_size;
                    zs.next_out  = result;
                    zs.avail_out = uncomp_sz;
                    if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                        int r = inflate(&zs, Z_FINISH);
                        inflateEnd(&zs);
                        if (r == Z_STREAM_END) {
                            if (filesize) *filesize = uncomp_sz;
                        } else {
                            free(result);
                            result = NULL;
                        }
                    } else {
                        free(result);
                        result = NULL;
                    }
                } else {
                    free(result);
                    result = NULL;
                }
                free(comp);
            }
            break;
        }

        fseek(fp, comp_size, SEEK_CUR);
    }

    fclose(fp);
    return result;
}

int ps3_zip_extract_pce(const char *zip_path, char *out_extracted_path, size_t max_len)
{
    char *entry = find_possible_filename_in_zip((char *)zip_path);
    if (!entry || !entry[0]) return -1;
    size_t sz = 0;
    unsigned char *data = extract_file_in_memory((char *)zip_path, entry, &sz);
    if (!data || sz == 0) return -1;
    snprintf(out_extracted_path, max_len, "%s/tmp_rom.pce", PS3_DATA_ROOT);
    FILE *f = fopen(out_extracted_path, "wb");
    if (!f) {
        free(data);
        return -1;
    }
    size_t written = fwrite(data, 1, sz, f);
    fclose(f);
    free(data);
    return (written == sz) ? 0 : -1;
}

void ps3_zip_cleanup_temp(void)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/tmp_rom.pce", PS3_DATA_ROOT);
    unlink(tmp);
}
