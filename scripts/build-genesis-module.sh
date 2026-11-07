#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CORE_DIR=${GENESIS_CORE_DIR:-"$ROOT/build/cores/genesis-plus-gx"}
OUT=${GENESIS_MODULE_OUT:-"$ROOT/build/modules/genesis.so"}
CC=${CC:-cc}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 1)}

if [ ! -f "$CORE_DIR/Makefile.libretro" ]; then
    mkdir -p "$(dirname "$CORE_DIR")"
    git clone --depth 1 https://github.com/libretro/Genesis-Plus-GX.git "$CORE_DIR"
fi

make -C "$CORE_DIR" -f Makefile.libretro platform=unix -j"$JOBS"

OBJECTS=$(make -C "$CORE_DIR" -f Makefile.libretro platform=unix -pn 2>/dev/null |
    awk -F' := ' '/^OBJECTS :=/{print $2; exit}')
if [ -z "$OBJECTS" ]; then
    echo "build-genesis-module: cannot determine core objects" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")" "$ROOT/build/libretro"
ADAPTER_OBJ="$ROOT/build/libretro/libretro_module.o"
"$CC" -std=c11 -O2 -g -Wall -Wextra -fPIC -fvisibility=hidden \
    -I"$ROOT/include" -I"$ROOT/modules/libretro" \
    -c "$ROOT/modules/libretro/libretro_module.c" -o "$ADAPTER_OBJ"

CORE_OBJS=
for obj in $OBJECTS; do
    CORE_OBJS="$CORE_OBJS $CORE_DIR/$obj"
done

"$CC" -shared -Wl,-Bsymbolic -o "$OUT" "$ADAPTER_OBJ" $CORE_OBJS -lm
echo "$OUT"
