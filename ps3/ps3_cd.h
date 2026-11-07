#ifndef PS3_CD_H
#define PS3_CD_H

int ps3_cd_is_disc(const char *path);
int ps3_cd_mount(const char *path);
void ps3_cd_unmount(void);
void ps3_cd_mix(unsigned char *buf, int frames);

#endif
