#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST=${RPCS3_DIR:-"$ROOT/build/ps3"}
APP="$DEST/rpcs3.AppImage"
FW="$DEST/PS3UPDAT.PUP"
XDG_CONFIG="$DEST/xdg/config"
XDG_DATA="$DEST/xdg/data"
XDG_CACHE="$DEST/xdg/cache"
API="https://api.github.com/repos/RPCS3/rpcs3-binaries-linux/releases/latest"
FW_PAGE="https://www.playstation.com/en-us/support/hardware/ps3/system-software/"
FW_FALLBACK="http://dus01.ps3.update.playstation.net/update/ps3/image/us/2026_0318_a2b60b6ac1d2e49e230144345616927c/PS3UPDAT.PUP"

mkdir -p "$DEST" "$XDG_CONFIG" "$XDG_DATA" "$XDG_CACHE"

if [ ! -x "$APP" ]; then
    echo "get-rpcs3: downloading the latest RPCS3 Linux build"
    url=$(curl -fsSL "$API" | python3 -c '
import json, sys
release = json.load(sys.stdin)
for asset in release.get("assets", []):
    if asset.get("name", "").endswith("_linux64.AppImage"):
        print(asset["browser_download_url"])
        break
else:
    raise SystemExit("no Linux AppImage asset in latest release")
')
    curl -fL --retry 3 -o "$APP.tmp" "$url"
    chmod +x "$APP.tmp"
    mv "$APP.tmp" "$APP"
fi

if [ ! -f "$FW" ]; then
    url=${PS3_FIRMWARE_URL:-}
    if [ -z "$url" ]; then
        url=$(curl -fsSL "$FW_PAGE" 2>/dev/null |
            grep -oE 'https?://[^"<> ]+PS3UPDAT\.PUP' | head -n 1 || true)
    fi
    if [ -z "$url" ]; then
        url="$FW_FALLBACK"
    fi
    echo "get-rpcs3: downloading PS3 firmware from $url"
    curl -fL --retry 3 -o "$FW.tmp" "$url"
    mv "$FW.tmp" "$FW"
fi

VERSION_FILE="$XDG_CONFIG/rpcs3/dev_flash/vsh/etc/version.txt"
if [ ! -f "$VERSION_FILE" ]; then
    echo "get-rpcs3: installing PS3 firmware into $DEST/xdg"
    : > "$DEST/install.log"
    XDG_CONFIG_HOME="$XDG_CONFIG" XDG_DATA_HOME="$XDG_DATA" \
        XDG_CACHE_HOME="$XDG_CACHE" \
        timeout 180 "$APP" --installfw "$FW" --headless \
        >"$DEST/install.log" 2>&1 || true
    if [ ! -f "$VERSION_FILE" ]; then
        echo "get-rpcs3: firmware installation failed; see $DEST/install.log" >&2
        tail -n 20 "$DEST/install.log" >&2 || true
        exit 1
    fi
fi

version=$(sed -n 's/^release:\([^:]*\).*/\1/p' "$VERSION_FILE" | head -n 1)
echo "get-rpcs3: RPCS3 ready at $APP"
echo "get-rpcs3: firmware $version installed"
echo "get-rpcs3: run ./scripts/run-ps3.sh to boot the PS3 XMB"
