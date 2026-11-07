#include "ps3_platform.h"
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log_fp = NULL;

void ps3_log_init(void)
{
    if (!g_log_fp) {
        g_log_fp = fopen(PS3_LOG_PATH, "w");
        if (g_log_fp) {
            fprintf(g_log_fp, "=== %s v%s started ===\n", PS3_APP_NAME, PS3_APP_VERSION);
            fflush(g_log_fp);
        }
    }
}

void ps3_log_close(void)
{
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

void ps3_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);

    if (g_log_fp) {
        va_start(ap, fmt);
        vfprintf(g_log_fp, fmt, ap);
        va_end(ap);
        fflush(g_log_fp);
    }
}
