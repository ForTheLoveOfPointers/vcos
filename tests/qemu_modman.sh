#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$ROOT/buildroot/output/images/vcos.img"
PKG="$ROOT/build/vmod/org.vcos.nes-1.0.0.vmod"
TMP="$(mktemp -d /tmp/vcos-modman-test.XXXXXX)"
SERIAL="$TMP/serial.sock"
MON="$TMP/monitor.sock"
LOG="$TMP/serial.log"
QPID=""

cleanup() {
	[ -n "$QPID" ] && kill "$QPID" 2>/dev/null || true
	[ -n "$QPID" ] && wait "$QPID" 2>/dev/null || true
	if [ -n "${VCOS_TEST_KEEP:-}" ]; then
		echo "qemu-modman: kept $TMP" >&2
	else
		rm -rf "$TMP"
	fi
}
trap cleanup EXIT

if [ ! -f "$IMG" ]; then
	echo "qemu-modman: missing $IMG; run ./scripts/build-image.sh" >&2
	exit 1
fi
if [ ! -f "$PKG" ]; then
	echo "qemu-modman: missing $PKG; run make vmod" >&2
	exit 1
fi

SIGN_KEY="${VCOS_SIGNING_KEY:-$ROOT/keys/private/vcos-signing-private.pem}"
INSTALL_ARGS="--allow-unsigned"
if [ -f "$SIGN_KEY" ]; then
	VCOS_SIGNING_KEY="$SIGN_KEY" "$ROOT/scripts/sign-vmod.sh" "$PKG" >/dev/null
	INSTALL_ARGS=""
fi
for tool in qemu-system-x86_64 python3 openssl; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "qemu-modman: $tool is required" >&2
		exit 1
	fi
done

OVMF_CODE=""
OVMF_VARS=""
for code in \
	/usr/share/OVMF/OVMF_CODE_4M.fd \
	/usr/share/OVMF/OVMF_CODE.fd \
	/usr/share/edk2/x64/OVMF_CODE.fd
do
	if [ -f "$code" ]; then
		OVMF_CODE="$code"
		break
	fi
done
for vars in \
	/usr/share/OVMF/OVMF_VARS_4M.fd \
	/usr/share/OVMF/OVMF_VARS.fd \
	/usr/share/edk2/x64/OVMF_VARS.fd
do
	if [ -f "$vars" ]; then
		OVMF_VARS="$vars"
		break
	fi
done
if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS" ]; then
	echo "qemu-modman: OVMF is required" >&2
	exit 1
fi

SHARE="$TMP/share"
mkdir -p "$SHARE"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
	-keyout "$TMP/key.pem" -out "$SHARE/ca.pem" -subj "/CN=10.0.2.2" \
	-addext "subjectAltName=IP:10.0.2.2" \
	-addext "basicConstraints=critical,CA:TRUE" \
	-addext "keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign" \
	>/dev/null 2>&1

OVERLAY="$TMP/disk.img"
cp --reflink=auto --sparse=always "$IMG" "$OVERLAY"

VARS_COPY="$TMP/vars.fd"
cp "$OVMF_VARS" "$VARS_COPY"
truncate -s "$(stat -c%s "$OVMF_CODE")" "$VARS_COPY"

qemu-system-x86_64 -machine q35 -m 1024 \
	-drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
	-drive if=pflash,format=raw,file="$VARS_COPY" \
	-drive file="$OVERLAY",format=raw,if=none,id=disk0 \
	-device virtio-blk-pci,drive=disk0,bootindex=0 \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0 \
	-vga std -display none \
	-serial "unix:$SERIAL,server=on,wait=off" \
	-monitor "unix:$MON,server=on,wait=off" \
	-virtfs "local,path=$ROOT/build/vmod,mount_tag=hostshare,security_model=none,readonly=on" \
	-virtfs "local,path=$SHARE,mount_tag=testshare,security_model=none,readonly=on" \
	</dev/null >"$TMP/qemu.log" 2>&1 &
QPID=$!

python3 - "$SERIAL" "$LOG" "$INSTALL_ARGS" "$ROOT/build/vmod" \
	"$SHARE/ca.pem" "$TMP/key.pem" <<'PY'
import http.server
import os
import select
import socket
import ssl
import sys
import threading
import time

serial_path, log_path, install_args, serve_dir, cert, key = sys.argv[1:7]

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=serve_dir, **kwargs)

    def log_message(self, *args):
        pass

httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), QuietHandler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
threading.Thread(target=httpd.serve_forever, daemon=True).start()
online_port = httpd.server_port
print(f"qemu-modman: HTTPS test server on port {online_port}")

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 20
while True:
    try:
        sock.connect(serial_path)
        break
    except OSError:
        if time.time() >= deadline:
            raise
        time.sleep(0.1)

buffer = b""
log = open(log_path, "wb")


def read_until(marker, timeout=90):
    global buffer
    if isinstance(marker, str):
        marker = marker.encode()
    deadline = time.time() + timeout
    while marker not in buffer:
        remaining = deadline - time.time()
        if remaining <= 0:
            raise SystemExit(
                f"qemu-modman: timed out waiting for {marker!r}\n"
                + buffer.decode("utf-8", "replace")[-4000:]
            )
        ready, _, _ = select.select([sock], [], [], min(remaining, 0.5))
        if not ready:
            continue
        data = sock.recv(65536)
        if not data:
            raise SystemExit("qemu-modman: serial connection closed")
        buffer += data
        log.write(data)
        log.flush()
    index = buffer.index(marker) + len(marker)
    out = buffer[:index]
    buffer = buffer[index:]
    return out.decode("utf-8", "replace")


def run(command, name, timeout=90):
    marker = f"__VCOS_{name}__"
    sock.sendall(f"{command}; echo __VCOS_\"\"{name}__:$?\n".encode())
    text = read_until(f"{marker}:", timeout)
    result = read_until("\n", 10).strip()
    if result != "0":
        raise SystemExit(
            f"qemu-modman: command failed ({name}, status {result})\n"
            + text[-4000:]
        )


read_until("login:", 90)
sock.sendall(b"root\n")
read_until("# ", 30)

run("mkdir -p /mnt /testca", "mkdir")
run("mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt", "mount")
run("mount -t 9p -o trans=virtio,version=9p2000.L testshare /testca",
    "mount-test")
run("ip link set eth0 up", "net-up")
run("if ! ip -4 addr show eth0 | grep -q 'inet '; then "
    "udhcpc -i eth0 -n -q -t 10 -T 2; fi", "dhcp", 30)
run("ip -4 addr show eth0 | grep -q 'inet '", "net-address")
run("vcos-modman info /mnt/org.vcos.nes-1.0.0.vmod", "info")
run(f"vcos-modman install /mnt/org.vcos.nes-1.0.0.vmod {install_args}", "install")
run("vcos-modman list", "list")
run("test -f /var/lib/vcos/modules/org.vcos.nes/current/payload/module.so", "installed")
run("sync", "sync")
sock.sendall(b"reboot\n")
read_until("login:", 120)
sock.sendall(b"root\n")
read_until("# ", 30)
run("mkdir -p /testca", "mkdir-testca")
run("mount -t 9p -o trans=virtio,version=9p2000.L testshare /testca",
    "mount-test-after-reboot")
run("vcos-modman list | grep -q '^org.vcos.nes '", "list-after-reboot")
run("test -f /var/lib/vcos/modules/org.vcos.nes/current/payload/module.so", "persisted")
run("/etc/init.d/S90vcos stop", "stop")
run("vcos-modman run org.vcos.nes /var/lib/vcos/games/table_tennis.nes "
    "--backend null --frames 1 --audio none --no-pace --no-kmsg", "run", 120)
run("grep -q 'module=org.vcos.nes' /var/lib/vcos/logs/module-host.log", "log")
run("vcos-modman remove org.vcos.nes", "remove")
run("test ! -e /var/lib/vcos/modules/org.vcos.nes", "removed")

online_url = f"https://10.0.2.2:{online_port}/org.vcos.nes-1.0.0.vmod"
run(f"VCOS_CA_BUNDLE=/testca/ca.pem vcos-modman install {online_url} "
    f"{install_args}", "online-install", 120)
run("vcos-modman list | grep -q '^org.vcos.nes '", "online-list")
run("test -f /var/lib/vcos/modules/org.vcos.nes/current/payload/module.so",
    "online-installed")
run("vcos-modman remove org.vcos.nes", "online-remove")
run("test ! -e /var/lib/vcos/modules/org.vcos.nes", "online-removed")

log.close()
sock.close()
print("qemu-modman: serial session passed")
PY

echo "qemu_modman: ok"
