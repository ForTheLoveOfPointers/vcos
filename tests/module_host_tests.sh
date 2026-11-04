#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
HOST="$ROOT/build/module-host/vcos-module-host"
NULL_MODULE="$ROOT/build/modules/null.so"
NES_MODULE="$ROOT/build/modules/nes.so"
GAME="$ROOT/games/table_tennis.nes"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail()
{
    echo "module_host_tests: FAIL: $*" >&2
    exit 1
}

test -x "$HOST" || fail "missing $HOST"
test -f "$NULL_MODULE" || fail "missing $NULL_MODULE"
test -f "$NES_MODULE" || fail "missing $NES_MODULE"

"$HOST" \
    --module "$NULL_MODULE" \
    --content "$ROOT/README.md" \
    --backend ppm \
    --frames 3 \
    --screenshot "$TMP/null.ppm" \
    --audio none \
    --no-pace \
    --no-kmsg \
    --log "$TMP/null.log"
test -s "$TMP/null.ppm" || fail "null module produced no frame"
grep -q 'module=org.vcos.null' "$TMP/null.log" || fail "missing null module log"
grep -q 'stopped after 3 frames' "$TMP/null.log" || fail "null module did not stop"

test -f "$GAME" || fail "missing test game $GAME"

"$HOST" \
    --module "$NES_MODULE" \
    --content "$GAME" \
    --backend ppm \
    --frames 30 \
    --screenshot "$TMP/nes30.ppm" \
    --audio none \
    --no-pace \
    --no-kmsg \
    --log "$TMP/nes.log" \
    --state "$TMP/nes.state"
test -s "$TMP/nes30.ppm" || fail "NES module produced no frame"
test -s "$TMP/nes.state" || fail "NES module produced no save state"
grep -q 'module=org.vcos.nes' "$TMP/nes.log" || fail "missing NES module log"

"$HOST" \
    --module "$NES_MODULE" \
    --content "$GAME" \
    --backend ppm \
    --frames 60 \
    --screenshot "$TMP/nes60.ppm" \
    --audio none \
    --no-pace \
    --no-kmsg \
    --log "$TMP/nes60.log"

"$HOST" \
    --module "$NES_MODULE" \
    --content "$GAME" \
    --backend ppm \
    --frames 30 \
    --screenshot "$TMP/nes30-fromstate.ppm" \
    --audio none \
    --no-pace \
    --no-kmsg \
    --log "$TMP/nes30s.log" \
    --load-state "$TMP/nes.state"

cmp "$TMP/nes60.ppm" "$TMP/nes30-fromstate.ppm" || \
    fail "save state did not reproduce the same frame"
grep -q 'state loaded' "$TMP/nes30s.log" || fail "missing state load log"

TARGET="$ROOT/buildroot/output/target"
TARGET_HOST="$TARGET/usr/bin/vcos-module-host"
TARGET_NES="$TARGET/usr/lib/vcos/modules/nes.so"
if command -v qemu-x86_64 >/dev/null 2>&1 && test -x "$TARGET_HOST" &&
    test -f "$TARGET_NES"; then
    qemu-x86_64 -L "$TARGET" "$TARGET_HOST" \
        --module "$TARGET_NES" \
        --content "$GAME" \
        --backend ppm \
        --frames 3 \
        --screenshot "$TMP/target-nes.ppm" \
        --audio none \
        --no-pace \
        --no-kmsg \
        --log "$TMP/target-nes.log"
    test -s "$TMP/target-nes.ppm" || fail "target module-host produced no frame"
    grep -q 'module=org.vcos.nes' "$TMP/target-nes.log" ||
        fail "target module-host did not load NES"
fi

echo "module_host_tests: ok"
