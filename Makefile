PS3DEV   ?= /mnt/d/ps3dev-sdk
PSL1GHT  ?= $(PS3DEV)/psl1ght
PORTLIBS ?= $(PS3DEV)/portlibs/ppu

CC       := $(PS3DEV)/ppu/bin/ppu-gcc
STRIP    := $(PS3DEV)/ppu/bin/ppu-strip
SPRX     := $(PS3DEV)/bin/sprxlinker
SELF     := $(PS3DEV)/bin/make_self

TARGET   := pce_ps3
HUGO_DIR := src/hugo
PS3_DIR  := ps3
SHIM_DIR := ps3/shim

HUGO_SRCS := \
	$(HUGO_DIR)/pce.c \
	$(HUGO_DIR)/hard_pce.c \
	$(HUGO_DIR)/h6280.c \
	$(HUGO_DIR)/optable.c \
	$(HUGO_DIR)/gfx.c \
	$(HUGO_DIR)/sprite.c \
	$(HUGO_DIR)/utils.c \
	$(HUGO_DIR)/sound.c \
	$(HUGO_DIR)/mix.c \
	$(HUGO_DIR)/cd.c \
	$(HUGO_DIR)/pcecd.c \
	$(HUGO_DIR)/hcd.c \
	$(HUGO_DIR)/list_rom.c \
	$(HUGO_DIR)/iniconfig.c \
	$(HUGO_DIR)/format.c \
	$(HUGO_DIR)/trans_fx.c \
	$(HUGO_DIR)/bios.c \
	$(HUGO_DIR)/lang.c \
	$(HUGO_DIR)/debug.c \
	$(HUGO_DIR)/dis.c \
	$(HUGO_DIR)/edit_ram.c \
	$(HUGO_DIR)/followop.c \
	$(HUGO_DIR)/bp.c

PS3_SRCS := \
	$(PS3_DIR)/ps3_main.c \
	$(PS3_DIR)/ps3_osd.c \
	$(PS3_DIR)/ps3_video.c \
	$(PS3_DIR)/ps3_audio.c \
	$(PS3_DIR)/ps3_cd.c \
	$(PS3_DIR)/ps3_zip.c \
	$(PS3_DIR)/ps3_config.c \
	$(PS3_DIR)/ps3_paths.c \
	$(PS3_DIR)/ps3_log.c \
	$(PS3_DIR)/ps3_sdlcompat.c

HUGO_OBJS := $(HUGO_SRCS:.c=.o)
PS3_OBJS  := $(PS3_SRCS:.c=.o)
PREBUILT_OBJS := $(PS3_DIR)/prebuilt/SDL_PSL1GHTrender.o
OBJS      := $(HUGO_OBJS) $(PS3_OBJS) $(PREBUILT_OBJS)

COMMON_INCLUDES := \
	-I$(SHIM_DIR) \
	-I$(PS3_DIR) \
	-I$(HUGO_DIR) \
	-I$(PORTLIBS)/include \
	-I$(PORTLIBS)/include/SDL2 \
	-I$(PORTLIBS)/include/freetype2 \
	-I$(PSL1GHT)/ppu/include \
	-I$(PS3DEV)/ppu/include

COMMON_WARN := \
	-Wall \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-unused-but-set-variable \
	-Wno-sign-compare \
	-Wno-char-subscripts \
	-Wno-parentheses \
	-Wno-format \
	-Wno-implicit-function-declaration \
	-Wno-incompatible-pointer-types \
	-Wno-pointer-sign \
	-Wno-int-conversion \
	-Wno-return-type \
	-Wno-sequence-point \
	-Wno-strict-aliasing \
	-Wno-maybe-uninitialized \
	-Wno-endif-labels \
	-Wno-comment \
	-Wno-switch

COMMON_DEFS := \
	-D__PS3__ \
	-DPS3_PORT \
	-DWORDS_BIGENDIAN=1 \
	-DBYTE_ORDER=BIG_ENDIAN \
	-DINLINE_STATIC="static inline" \
	-DFINAL_RELEASE \
	-DHAVE_CONFIG_H \
	-DSDL_MAIN_HANDLED

BASE_CFLAGS := -O2 -mcpu=cell -std=gnu99 -fcommon -fno-strict-aliasing -fsigned-char $(MACHDEP) $(COMMON_WARN) $(COMMON_INCLUDES) $(COMMON_DEFS)
HUGO_CFLAGS := $(BASE_CFLAGS) -include $(SHIM_DIR)/prelude.h
PS3_CFLAGS  := $(BASE_CFLAGS) -include $(SHIM_DIR)/prelude.h

LDFLAGS := $(MACHDEP) -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib -L$(PS3DEV)/ppu/lib

LIBS := \
	-lSDL2_ttf \
	-lSDL2_image \
	-lfreetype \
	-lpng \
	-ljpeg \
	-lSDL2 \
	-lgcm_sys \
	-lio \
	-laudio \
	-lsysutil \
	-lsysmodule \
	-lrsx \
	-lrt \
	-llv2 \
	-lm \
	-lz \
	-lc

.PHONY: all clean

all: $(TARGET).elf $(TARGET).self

$(TARGET).elf: $(OBJS)
	@echo "[LD]  $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).self: $(TARGET).elf
	@echo "[SELF] $@"
	@$(STRIP) $< -o $(TARGET).stripped.elf
	@$(SPRX) $(TARGET).stripped.elf
	@$(SELF) $(TARGET).stripped.elf $@

$(HUGO_DIR)/%.o: $(HUGO_DIR)/%.c $(SHIM_DIR)/config.h $(SHIM_DIR)/prelude.h $(SHIM_DIR)/m6502.h
	@echo "[CC]  $<"
	@$(CC) $(HUGO_CFLAGS) -c $< -o $@

$(PS3_DIR)/%.o: $(PS3_DIR)/%.c $(PS3_DIR)/ps3_platform.h $(PS3_DIR)/ps3_btn_icons.h $(SHIM_DIR)/config.h
	@echo "[CC]  $<"
	@$(CC) $(PS3_CFLAGS) -c $< -o $@

clean:
	rm -f $(HUGO_OBJS) $(PS3_OBJS) $(TARGET).elf $(TARGET).stripped.elf $(TARGET).self
