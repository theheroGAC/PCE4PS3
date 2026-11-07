#!/usr/bin/env bash
set -euo pipefail

export PS3DEV="${PS3DEV:-/mnt/d/ps3dev-sdk}"
export PSL1GHT="${PSL1GHT:-$PS3DEV/psl1ght}"
export PORTLIBS="${PORTLIBS:-$PS3DEV/portlibs/ppu}"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PSL1GHT/host/bin:$PATH"

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

TITLE_ID="PCE4PS300"
CONTENT_ID="UP0001-${TITLE_ID}_00-0000000000000000"
PKG_DIR="$ROOT_DIR/build/pkg/$TITLE_ID"
DIST_DIR="$ROOT_DIR/dist"

if [ ! -f "$ROOT_DIR/pce_ps3.elf" ]; then
    "$ROOT_DIR/build.sh"
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/USRDIR/skins"
mkdir -p "$DIST_DIR"

cp -f "$ROOT_DIR/pce_ps3.elf" "$PKG_DIR/USRDIR/pce_ps3.elf"
ppu-strip "$PKG_DIR/USRDIR/pce_ps3.elf"
sprxlinker "$PKG_DIR/USRDIR/pce_ps3.elf"
make_self_npdrm "$PKG_DIR/USRDIR/pce_ps3.elf" "$PKG_DIR/USRDIR/EBOOT.BIN" "$CONTENT_ID"
rm -f "$PKG_DIR/USRDIR/pce_ps3.elf"

sfo -f "$ROOT_DIR/sfo.xml" "$PKG_DIR/PARAM.SFO" --title="PCE-PS3 by theheroGAC" --appid="$TITLE_ID"

cp -f "$ROOT_DIR/assets/ICON0.PNG" "$PKG_DIR/ICON0.PNG"
if [ -f "$ROOT_DIR/assets/PIC1.PNG" ]; then
    cp -f "$ROOT_DIR/assets/PIC1.PNG" "$PKG_DIR/PIC1.PNG"
fi
cp -f "$ROOT_DIR/assets/default.ttf" "$PKG_DIR/USRDIR/font.ttf"
cp -f "$ROOT_DIR/assets/default.ttf" "$PKG_DIR/USRDIR/skins/default.ttf"

pkg --contentid="$CONTENT_ID" "$PKG_DIR/" "$DIST_DIR/PCE4PS3.pkg"
cp -f "$DIST_DIR/PCE4PS3.pkg" "$DIST_DIR/PCE4PS3.gnpdrm.pkg"
package_finalize "$DIST_DIR/PCE4PS3.gnpdrm.pkg"

echo "Packages ready in $DIST_DIR:"
ls -lh "$DIST_DIR/PCE4PS3.pkg" "$DIST_DIR/PCE4PS3.gnpdrm.pkg"
