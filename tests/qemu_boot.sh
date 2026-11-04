#!/usr/bin/env bash
#
# Boot the generated disk image under QEMU, drive it with the emulated
# keyboard, and check the first-demo flow:
#
#   menu -> cartridge -> menu -> controller poweroff -> reboot -> data check
#
# The second boot must render the shell and the versioned save file on the
# data partition must advance. This proves that saves survive a reboot.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$ROOT/buildroot/output/images/vcos.img"
TMP="$(mktemp -d /tmp/vcos-qemu-test.XXXXXX)"
DISK="$TMP/vcos.img"
MON="$TMP/monitor.sock"
SERIAL="$TMP/serial.log"

cleanup() {
	if [ -n "${VCOS_TEST_KEEP:-}" ]; then
		echo "qemu-boot: kept $TMP" >&2
	else
		rm -rf "$TMP"
	fi
}
trap cleanup EXIT

if [ ! -f "$IMG" ]; then
	echo "qemu-boot: missing $IMG; run ./scripts/build-image.sh" >&2
	exit 1
fi
for tool in qemu-system-x86_64 socat python3 debugfs dd cut; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "qemu-boot: $tool is required" >&2
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

QPID=""
VARS_COPY="$TMP/vars.fd"

start_vm() {
	: >"$SERIAL"
	rm -f "$MON"
	cp "$OVMF_VARS" "$VARS_COPY"
	truncate -s "$(stat -c%s "$OVMF_CODE")" "$VARS_COPY"
	qemu-system-x86_64 -machine q35 -m 1024 \
		-drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
		-drive if=pflash,format=raw,file="$VARS_COPY" \
		-drive file="$DISK",format=raw,if=virtio \
		-vga std -display none -serial "file:$SERIAL" \
		-monitor "unix:$MON,server,nowait" </dev/null \
		>"$TMP/qemu.log" 2>&1 &
	QPID=$!
}

wait_for_shell() {
	for _ in $(seq 1 60); do
		if grep -q "Starting VCOS: ok" "$SERIAL" 2>/dev/null; then
			sleep 3
			return 0
		fi
		sleep 1
	done
	echo "qemu-boot: runtime did not start; serial log follows" >&2
	cat "$SERIAL" >&2
	return 1
}

monitor() {
	printf '%s\n' "$1" | socat - UNIX-CONNECT:"$MON" >/dev/null
}

stop_vm() {
	monitor "sendkey shift_r 500"
	for _ in $(seq 1 60); do
		kill -0 "$QPID" 2>/dev/null || break
		sleep 1
	done
	if kill -0 "$QPID" 2>/dev/null; then
		echo "qemu-boot: controller shutdown did not power off the VM" >&2
		kill "$QPID" 2>/dev/null || true
		wait "$QPID" 2>/dev/null || true
		QPID=""
		return 1
	fi
	wait "$QPID" 2>/dev/null || true
	QPID=""
}

data_geometry() {
	python3 - "$DISK" <<'PY'
import struct
import sys

with open(sys.argv[1], 'rb') as f:
    f.seek(512)
    header = f.read(92)
    if header[:8] != b'EFI PART':
        raise SystemExit('not a GPT image')
    entries_lba = struct.unpack_from('<Q', header, 72)[0]
    count = struct.unpack_from('<I', header, 80)[0]
    size = struct.unpack_from('<I', header, 84)[0]
    f.seek(entries_lba * 512)
    for _ in range(count):
        entry = f.read(size)
        if entry[:16] == b'\x00' * 16:
            continue
        start = struct.unpack_from('<Q', entry, 32)[0]
        end = struct.unpack_from('<Q', entry, 40)[0]
        name = entry[56:128].decode('utf-16-le', 'ignore').rstrip('\x00')
        if name == 'data':
            print(start, end - start + 1)
            raise SystemExit(0)
raise SystemExit('data partition not found')
PY
}

read_counter() {
	local geometry start count
	geometry="$(data_geometry)"
	start="$(echo "$geometry" | cut -d' ' -f1)"
	count="$(echo "$geometry" | cut -d' ' -f2)"
	dd if="$DISK" of="$TMP/data.ext4" bs=512 skip="$start" count="$count" status=none
	rm -f "$TMP/save.bin"
	debugfs -R "dump /save.bin $TMP/save.bin" "$TMP/data.ext4" >/dev/null 2>&1
	python3 - "$TMP/save.bin" <<'PY'
import sys
try:
    data = open(sys.argv[1], 'rb').read()
except OSError:
    raise SystemExit('save.bin missing from data partition')
if len(data) != 28 or data[:4] != b'VCS1':
    raise SystemExit('save.bin has no VCS1 header')
print(data[12])
PY
}

cp --reflink=auto --sparse=always "$IMG" "$DISK"

if [ -n "$OVMF_CODE" ] && [ -n "$OVMF_VARS" ]; then
	UEFI=1
else
	UEFI=0
fi

echo "qemu-boot: first boot ($([ "$UEFI" = 1 ] && echo UEFI || echo BIOS))"
start_vm
wait_for_shell
monitor "screendump $TMP/shell1.ppm"
monitor "sendkey ret 500"
sleep 4
monitor "screendump $TMP/demo.ppm"
monitor "sendkey ret 500"
sleep 4
monitor "screendump $TMP/shell2.ppm"
monitor "sendkey down 500"
sleep 1
monitor "sendkey ret 500"
sleep 4
monitor "screendump $TMP/music.ppm"
monitor "sendkey ret 500"
sleep 3
monitor "screendump $TMP/shell4.ppm"
stop_vm
FIRST_SAVED="$(read_counter)"

echo "qemu-boot: reboot, checking save persistence"
start_vm
wait_for_shell
monitor "screendump $TMP/shell3.ppm"
stop_vm
SECOND_SAVED="$(read_counter)"
if [ "$SECOND_SAVED" -le "$FIRST_SAVED" ]; then
	echo "qemu-boot: data counter did not advance ($FIRST_SAVED -> $SECOND_SAVED)" >&2
	exit 1
fi

python3 - "$TMP" "$FIRST_SAVED" "$SECOND_SAVED" <<'PY'
import sys
from pathlib import Path

tmp = Path(sys.argv[1])


def read_ppm(name):
    data = (tmp / name).read_bytes()
    if not data.startswith(b'P6'):
        raise SystemExit(f'{name} is not a P6 PPM')
    parts = data.split(b'\n', 3)
    width, height = map(int, parts[1].split())
    pixels = parts[3][: width * height * 3]
    return width, height, pixels


def summarize(name):
    w, h, px = read_ppm(name)
    colors = {px[i:i + 3] for i in range(0, len(px), 3)}
    return w, h, px, colors


w1, h1, shell1, colors1 = summarize('shell1.ppm')
w2, h2, demo, _ = summarize('demo.ppm')
w3, h3, shell2, _ = summarize('shell2.ppm')
w4, h4, shell3, colors3 = summarize('shell3.ppm')
w5, h5, music, _ = summarize('music.ppm')
w6, h6, shell4, _ = summarize('shell4.ppm')

if len({(w1, h1), (w2, h2), (w3, h3), (w4, h4), (w5, h5), (w6, h6)}) != 1:
    raise SystemExit('screenshot size changed')

if len(colors1) > 3:
    raise SystemExit(f'shell frame has {len(colors1)} colours, expected <= 3')
if demo == shell1:
    raise SystemExit('demo screenshot is identical to the shell')
if shell2 == demo:
    raise SystemExit('did not return to the shell after the demo')
if len(colors3) > 3:
    raise SystemExit(f'second boot did not render the shell ({len(colors3)} colours)')
if music == shell2:
    raise SystemExit('library menu did not launch the second cartridge')
if shell4 == music:
    raise SystemExit('second cartridge did not return to the shell')

print('qemu-boot: menu -> two cartridges -> menu ok')
print(f'qemu-boot: save persisted across reboot (counter {sys.argv[2]} -> {sys.argv[3]})')
PY
