#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/runtime/vcos"
LIB="$(mktemp -d /tmp/vcos-test.XXXXXX)"
trap 'rm -rf "$LIB"' EXIT

cp "$ROOT/build/guest/shell.nes" "$LIB/"
cp "$ROOT/build/guest/demo.nes" "$LIB/"
cp "$ROOT/build/guest/music.nes" "$LIB/"

echo "integration: booting shell (headless)"
"$BIN" --rom "$LIB/shell.nes" --lib "$LIB" --backend ppm --frames 4 \
    --screenshot "$LIB/shell.ppm" --verbose >"$LIB/shell.log" 2>&1

test -s "$LIB/shell.ppm"
test -f "$LIB/save.bin"
grep -q "services: saved" "$LIB/shell.log"

python3 - "$LIB/shell.ppm" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'rb') as f:
    if f.readline().strip() != b'P6':
        raise SystemExit('not a P6 PPM')
    w, h = map(int, f.readline().split())
    maxv = int(f.readline())
    if (w, h, maxv) != (256, 240, 255):
        raise SystemExit('unexpected PPM dimensions')
    data = f.read()
colors = {data[i:i+3] for i in range(0, len(data), 3)}
if len(colors) < 2:
    raise SystemExit('shell frame is blank')
print(f'integration: shell frame has {len(colors)} colours')
PY

FIRST_COUNT="$(python3 -c 'import sys;d=open(sys.argv[1],"rb").read();assert d[:4]==b"VCS1";print(d[12])' "$LIB/save.bin")"

echo "integration: shell launches the demo"
"$BIN" --rom "$LIB/shell.nes" --lib "$LIB" --backend null --frames 24 \
    --press 6:a --verbose >"$LIB/launch.log" 2>&1
grep -q "services: launch request" "$LIB/launch.log"
grep -q "system: loading .*demo.nes" "$LIB/launch.log"

echo "integration: booting shell again (save persistence)"
"$BIN" --rom "$LIB/shell.nes" --lib "$LIB" --backend null --frames 4 \
    --verbose >"$LIB/shell2.log" 2>&1
SECOND_COUNT="$(python3 -c 'import sys;d=open(sys.argv[1],"rb").read();assert d[:4]==b"VCS1";print(d[12])' "$LIB/save.bin")"
if [ "$SECOND_COUNT" -le "$FIRST_COUNT" ]; then
    echo "integration: boot counter did not advance ($FIRST_COUNT -> $SECOND_COUNT)" >&2
    exit 1
fi

echo "integration: library menu launches the music cartridge"
rm -f "$LIB/tune.wav"
"$BIN" --rom "$LIB/shell.nes" --lib "$LIB" --backend null --frames 90 \
    --press 10:down --press 18:a --audio "wav:$LIB/tune.wav" \
    --verbose >"$LIB/music.log" 2>&1
grep -q "services: library\[1\] MUSIC" "$LIB/music.log"
grep -q "system: loading .*music.nes" "$LIB/music.log"
test -s "$LIB/tune.wav"
python3 - "$LIB/tune.wav" <<'PY2'
import struct
import sys
data = open(sys.argv[1], 'rb').read()
if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
    raise SystemExit('bad WAV header')
size = struct.unpack('<I', data[40:44])[0]
if size == 0 or len(data) < 44 + size:
    raise SystemExit('empty WAV payload')
if not any(data[44:44 + size]):
    raise SystemExit('silent WAV payload')
PY2

echo "integration: shell select powers off"
"$BIN" --rom "$LIB/shell.nes" --lib "$LIB" --backend null --frames 30 \
    --press 10:select --verbose >"$LIB/shutdown.log" 2>&1
grep -q "services: shutdown request" "$LIB/shutdown.log"

echo "integration: demo returns to shell on Start"
"$BIN" --rom "$LIB/demo.nes" --lib "$LIB" --backend null --frames 40 \
    --press 8:start --verbose >"$LIB/demo.log" 2>&1
grep -q "services: shell request" "$LIB/demo.log"
grep -q "system: loading" "$LIB/demo.log"

echo "integration: ok"
