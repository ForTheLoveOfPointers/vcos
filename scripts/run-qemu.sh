#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGES="$ROOT/buildroot/output/images"
IMG="$IMAGES/vcos.img"
KERNEL="$IMAGES/bzImage"
ROOTFS="$IMAGES/rootfs.ext2"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "run-qemu: qemu-system-x86_64 is not installed" >&2
    exit 1
fi

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

QEMU_EXTRA=()
if [ -n "${VCOS_SHARE:-}" ]; then
    SHARE_PATH="$(readlink -f "$VCOS_SHARE")"
    if [ ! -d "$SHARE_PATH" ]; then
        echo "run-qemu: VCOS_SHARE is not a directory: $VCOS_SHARE" >&2
        exit 1
    fi
    QEMU_EXTRA+=(-virtfs "local,path=$SHARE_PATH,mount_tag=hostshare,security_model=none,readonly=on")
fi

if [ -f "$IMG" ] && [ -n "$OVMF_CODE" ]; then
    VARS_COPY="$(mktemp /tmp/vcos-ovmf-vars.XXXXXX)"
    CODE_SIZE="$(stat -c%s "$OVMF_CODE")"
    if [ -n "$OVMF_VARS" ]; then
        cp "$OVMF_VARS" "$VARS_COPY"
        truncate -s "$CODE_SIZE" "$VARS_COPY"
    else
        dd if=/dev/zero of="$VARS_COPY" bs=1 count=0 seek="$CODE_SIZE" status=none
    fi
    echo "run-qemu: booting $IMG with UEFI"
    exec qemu-system-x86_64 \
        -machine q35 \
        -m 1024 \
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,file="$VARS_COPY" \
        -drive file="$IMG",format=raw,if=none,id=disk0 \
        -device virtio-blk-pci,drive=disk0,bootindex=0 \
        -netdev user,id=net0 \
        -device virtio-net-pci,netdev=net0 \
        -vga std \
        "${QEMU_EXTRA[@]}" \
        -serial mon:stdio
fi

if [ -f "$KERNEL" ] && [ -f "$ROOTFS" ]; then
    echo "run-qemu: no disk image; booting bzImage + rootfs.ext2 directly"
    exec qemu-system-x86_64 \
        -machine q35 \
        -m 1024 \
        -kernel "$KERNEL" \
        -drive file="$ROOTFS",format=raw,if=virtio \
        -append "root=/dev/vda rw rootfstype=ext4 console=tty1 console=ttyS0,115200 loglevel=4" \
        -netdev user,id=net0 \
        -device virtio-net-pci,netdev=net0 \
        -vga std \
        "${QEMU_EXTRA[@]}" \
        -serial mon:stdio
fi

echo "run-qemu: no usable images found (install ovmf or run scripts/build-image.sh)" >&2
exit 1
