#!/bin/sh
set -eu

BINARIES_DIR="${1:-${BINARIES_DIR}}"
BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
EFI_DIR="${BINARIES_DIR}/efi-part"

mkdir -p "${EFI_DIR}/EFI/BOOT" "${EFI_DIR}/boot"

GRUB_EFI=""
for candidate in \
	"${BINARIES_DIR}/grub.efi" \
	"${BINARIES_DIR}/grub-efi.img" \
	"${BINARIES_DIR}/EFI/BOOT/BOOTX64.EFI" \
	"${EFI_DIR}/EFI/BOOT/BOOTX64.EFI" \
	"${EFI_DIR}/EFI/BOOT/bootx64.efi"
do
	if [ -f "${candidate}" ]; then
		GRUB_EFI="${candidate}"
		break
	fi
done

if [ -z "${GRUB_EFI}" ]; then
	echo "VCOS: no GRUB EFI binary found in ${BINARIES_DIR}" >&2
	echo "VCOS: enable BR2_TARGET_GRUB2 and BR2_TARGET_GRUB2_X86_64_EFI" >&2
	exit 1
fi

if [ "$(readlink -f "${GRUB_EFI}")" != "$(readlink -f "${EFI_DIR}/EFI/BOOT/BOOTX64.EFI")" ]; then
	install -m 0644 "${GRUB_EFI}" "${EFI_DIR}/EFI/BOOT/BOOTX64.EFI"
fi
install -m 0644 "${BOARD_DIR}/grub/grub.cfg" "${EFI_DIR}/EFI/BOOT/grub.cfg"
install -m 0644 "${BINARIES_DIR}/bzImage" "${EFI_DIR}/boot/bzImage"

DATA_IMG="${BINARIES_DIR}/vcos-data.ext4"
DATA_STAGE="$(mktemp -d)"
trap 'rm -rf "${DATA_STAGE}"' EXIT
mkdir -p "${DATA_STAGE}/games"
REPO_DIR="$(cd "${BOARD_DIR}/../../../.." && pwd)"
if ls "${REPO_DIR}"/games/*.nes >/dev/null 2>&1; then
	cp -f "${REPO_DIR}"/games/*.nes "${DATA_STAGE}/games/"
	cp -f "${REPO_DIR}/games/README.md" "${DATA_STAGE}/games/README.md" 2>/dev/null || true
	cp -f "${REPO_DIR}/games/LICENSE.table-tennis" \
		"${DATA_STAGE}/games/LICENSE.table-tennis" 2>/dev/null || true
fi
rm -f "${DATA_IMG}"
dd if=/dev/zero of="${DATA_IMG}" bs=1M count=128 status=none
mke2fs -q -t ext4 -L VCOSDATA -E root_owner=0:0 -d "${DATA_STAGE}" "${DATA_IMG}"

GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"
rm -rf "${GENIMAGE_TMP}"

genimage \
	--rootpath "${TARGET_DIR}" \
	--tmppath "${GENIMAGE_TMP}" \
	--inputpath "${BINARIES_DIR}" \
	--outputpath "${BINARIES_DIR}" \
	--config "${BOARD_DIR}/genimage.cfg"

echo "VCOS: disk image written to ${BINARIES_DIR}/vcos.img"
(cd "${BINARIES_DIR}" && sha256sum vcos.img > vcos.img.sha256)
echo "VCOS: checksum written to ${BINARIES_DIR}/vcos.img.sha256"
