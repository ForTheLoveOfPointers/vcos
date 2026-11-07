#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD:-"$ROOT/build"}
MODULE=${GENESIS_MODULE:-"$BUILD/modules/genesis.so"}
ROM=${GENESIS_ROM:-"$ROOT/tests/assets/genesis-hello-world.bin"}
HOST=${MODULE_HOST:-"$BUILD/module-host/vcos-module-host"}

for path in "$MODULE" "$ROM" "$HOST"; do
    if [ ! -f "$path" ]; then
        echo "genesis test: missing $path" >&2
        exit 1
    fi
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$HOST" --module "$MODULE" --content "$ROM" \
    --backend ppm --frames 120 --no-pace --audio none --no-kmsg \
    --screenshot "$TMP/frame.ppm" --state "$TMP/state.bin" \
    --log "$TMP/run.log"

python3 - "$TMP/frame.ppm" <<'PY'
import sys
from collections import Counter

path = sys.argv[1]
raw = open(path, "rb").read()
parts = raw.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6":
    raise SystemExit("invalid PPM")
width, height = map(int, parts[1].split())
if (width, height) != (320, 224):
    raise SystemExit(f"unexpected frame size {width}x{height}")
pixels = parts[3]
colors = Counter(zip(pixels[0::3], pixels[1::3], pixels[2::3]))
if len(colors) < 2:
    raise SystemExit("frame is blank")
print(f"genesis frame: {width}x{height}, {len(colors)} colors")
PY

test -s "$TMP/state.bin"

"$HOST" --module "$MODULE" --content "$ROM" \
    --backend ppm --frames 10 --no-pace --audio none --no-kmsg \
    --load-state "$TMP/state.bin" --screenshot "$TMP/loaded.ppm" \
    --log "$TMP/load.log"

test -s "$TMP/loaded.ppm"

"$HOST" --module "$MODULE" --content "$ROM" \
    --backend null --frames 30 --no-pace --audio "wav:$TMP/audio.wav" \
    --no-kmsg --log "$TMP/audio.log"

test -s "$TMP/audio.wav"
test "$(wc -c < "$TMP/audio.wav")" -gt 44

mkdir -p "$TMP/shell-lib" "$TMP/shell-data/games"
cp "$ROOT/build/guest/shell.nes" "$TMP/shell-lib/shell.nes"
cp "$ROM" "$TMP/shell-data/games/genesis-test.bin"

(
    cd "$TMP"
    "$ROOT/build/runtime/vcos" \
        --rom "$TMP/shell-lib/shell.nes" \
        --shell "$TMP/shell-lib/shell.nes" \
        --lib "$TMP/shell-lib" \
        --data "$TMP/shell-data" \
        --modules "$BUILD/modules" \
        --module-host "$HOST" \
        --backend null --frames 120 --audio none \
        --press 10:a --press 20:a --verbose > "$TMP/shell.log" 2>&1
)

grep -q 'services: system\[0\] GENESIS' "$TMP/shell.log"
grep -q 'GENESIS-TEST' "$TMP/shell.log"
grep -q 'system: launching .*genesis-test.bin' "$TMP/shell.log"
grep -q 'module=org.vcos.genesis' "$TMP/module-host.log"

echo "genesis tests: ok"
