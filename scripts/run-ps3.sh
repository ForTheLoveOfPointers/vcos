#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST=${RPCS3_DIR:-"$ROOT/build/ps3"}
APP="$DEST/rpcs3.AppImage"
XDG_CONFIG="$DEST/xdg/config"
XDG_DATA="$DEST/xdg/data"
XDG_CACHE="$DEST/xdg/cache"
VERSION_FILE="$XDG_CONFIG/rpcs3/dev_flash/vsh/etc/version.txt"

if [ ! -x "$APP" ] || [ ! -f "$VERSION_FILE" ]; then
    echo "run-ps3: run ./scripts/get-rpcs3.sh first" >&2
    exit 1
fi

export XDG_CONFIG_HOME="$XDG_CONFIG"
export XDG_DATA_HOME="$XDG_DATA"
export XDG_CACHE_HOME="$XDG_CACHE"

if [ "$#" -eq 0 ]; then
    exec "$APP" "$XDG_CONFIG/rpcs3/dev_flash/vsh/module/vsh.self"
fi

case "$1" in
    --xmb|--test)
        mode=$1
        shift
        ;;
    *)
        mode=game
        ;;
esac

if [ "$mode" = "--xmb" ]; then
    exec "$APP" "$XDG_CONFIG/rpcs3/dev_flash/vsh/module/vsh.self" "$@"
fi

if [ "$mode" = "--test" ]; then
    log="$DEST/smoke-test.log"
    : > "$log"
    : > "$XDG_CACHE/rpcs3/RPCS3.log" 2>/dev/null || true
    timeout 45 "$APP" --no-gui \
        "$XDG_CONFIG/rpcs3/dev_flash/vsh/module/vsh.self" >"$log" 2>&1 || true
    if grep -q "Vulkan: Renderer initialized on device" "$XDG_CACHE/rpcs3/RPCS3.log"; then
        echo "run-ps3: Vulkan renderer initialized"
        grep -m 1 "Found Vulkan-compatible GPU" "$XDG_CACHE/rpcs3/RPCS3.log"
        echo "run-ps3: smoke test log at $log"
        exit 0
    fi
    echo "run-ps3: smoke test failed; see $log and $XDG_CACHE/rpcs3/RPCS3.log" >&2
    exit 1
fi

exec "$APP" "$@"
