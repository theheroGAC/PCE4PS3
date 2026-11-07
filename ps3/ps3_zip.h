#ifndef PS3_ZIP_H
#define PS3_ZIP_H

#include <stddef.h>

char          *find_possible_filename_in_zip(char *zipfilename);
unsigned char *extract_file_in_memory(char *zipfilename, char *archivedfile, size_t *filesize);
int            ps3_zip_extract_pce(const char *zip_path, char *out_extracted_path, size_t max_len);
void           ps3_zip_cleanup_temp(void);

#endif
