#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
MODMAN="$ROOT/build/modman/vcos-modman"
MODULE="$ROOT/build/modules/null.so"
TMP=$(mktemp -d)
SERVER_PID=""

cleanup()
{
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

fail()
{
    echo "modman_online_tests: FAIL: $*" >&2
    exit 1
}

test -x "$MODMAN" || fail "missing $MODMAN"
test -f "$MODULE" || fail "missing $MODULE"
command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v openssl >/dev/null 2>&1 || fail "openssl is required"

SIGN_KEY="${VCOS_SIGNING_KEY:-$ROOT/keys/private/vcos-signing-private.pem}"
if [ ! -f "$SIGN_KEY" ]; then
    echo "modman_online_tests: skipped (no signing key)"
    exit 0
fi

mkdir -p "$TMP/serve" "$TMP/data"
PKG="$TMP/serve/org.vcos.null-1.0.0.vmod"
python3 "$ROOT/scripts/make-vmod.py" \
    --id org.vcos.null \
    --name "VCOS Null" \
    --version 1.0.0 \
    --module "$MODULE" \
    --system null \
    --extension .null \
    --description "null test module" \
    --out "$PKG" >/dev/null
VCOS_SIGNING_KEY="$SIGN_KEY" "$ROOT/scripts/sign-vmod.sh" "$PKG" >/dev/null

openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -keyout "$TMP/key.pem" -out "$TMP/ca.pem" -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign" \
    >/dev/null 2>&1

python3 - "$TMP/serve" "$TMP/ca.pem" "$TMP/key.pem" "$TMP/port" <<'PY' &
import http.server
import ssl
import sys

serve, cert, key, portfile = sys.argv[1:5]


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=serve, **kwargs)

    def log_message(self, *args):
        pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
server.socket = context.wrap_socket(server.socket, server_side=True)
with open(portfile, "w", encoding="utf-8") as out:
    out.write(str(server.server_port))
server.serve_forever()
PY
SERVER_PID=$!
for _ in $(seq 1 50); do
    [ -s "$TMP/port" ] && break
    sleep 0.1
done
[ -s "$TMP/port" ] || fail "HTTPS server did not start"
PORT=$(cat "$TMP/port")

if "$MODMAN" install "http://127.0.0.1:$PORT/org.vcos.null-1.0.0.vmod" \
    --root "$TMP/data" >"$TMP/http.out" 2>"$TMP/http.err"; then
    fail "install accepted an HTTP URL"
fi
grep -q 'https' "$TMP/http.err" || fail "missing HTTPS-only error"

VCOS_CA_BUNDLE="$TMP/ca.pem" "$MODMAN" install \
    "https://127.0.0.1:$PORT/org.vcos.null-1.0.0.vmod" \
    --root "$TMP/data" >"$TMP/install.out" 2>"$TMP/install.err"
grep -q 'signature verified' "$TMP/install.err" ||
    fail "online install did not verify the signature"
test -f "$TMP/data/modules/org.vcos.null/1.0.0/payload/module.so" ||
    fail "online module not installed"

"$MODMAN" list --root "$TMP/data" >"$TMP/list"
grep -q '^org.vcos.null 1.0.0 VCOS Null null$' "$TMP/list" ||
    fail "online install list output"

echo "modman_online_tests: ok"
