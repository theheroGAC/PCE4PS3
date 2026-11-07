#ifndef HUGO_SHIM_M6502_H
#define HUGO_SHIM_M6502_H

typedef union {
    struct { unsigned char H; unsigned char L; } B;
    unsigned short W;
} pair6502;

#endif
