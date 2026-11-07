#ifndef HUGO_PS3_PRELUDE_H
#define HUGO_PS3_PRELUDE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>

#undef stricmp

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

#ifndef MAX_INPUT
#define MAX_INPUT 256
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define _ hugo_underscore_buffer

extern unsigned char dump_snd;

#include "m6502.h"

#endif
