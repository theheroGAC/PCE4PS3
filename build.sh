#!/usr/bin/env bash
set -e

if [ -z "${PS3DEV:-}" ]; then
    if [ -d "/mnt/d/ps3dev-sdk" ]; then
        export PS3DEV="/mnt/d/ps3dev-sdk"
    elif [ -d "/usr/local/ps3dev" ]; then
        export PS3DEV="/usr/local/ps3dev"
    else
        echo "ERROR: PS3DEV toolchain not found." >&2
        exit 1
    fi
fi

export PSL1GHT="$PS3DEV/psl1ght"
export PORTLIBS="$PS3DEV/portlibs/ppu"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PSL1GHT/host/bin:$PATH"

echo "=== Building PCE-PS3 with PS3DEV=$PS3DEV ==="
make -j"$(nproc 2>/dev/null || echo 4)"
ls -lh pce_ps3.elf pce_ps3.self
