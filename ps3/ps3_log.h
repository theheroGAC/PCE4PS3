#ifndef PS3_LOG_H
#define PS3_LOG_H

void ps3_log_open(void);
void ps3_logf(const char *fmt, ...);
void ps3_log_close(void);

#endif
