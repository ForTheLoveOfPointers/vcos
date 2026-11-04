#!/bin/sh
set -eu

TARGET_DIR="${1:-${TARGET_DIR}}"

mkdir -p "${TARGET_DIR}/var/lib/vcos/modules" \
	"${TARGET_DIR}/var/lib/vcos/downloads/staging" \
	"${TARGET_DIR}/var/lib/vcos/manifests" \
	"${TARGET_DIR}/var/log" \
	"${TARGET_DIR}/usr/share/vcos"

if [ -f "${TARGET_DIR}/etc/init.d/S90vcos" ]; then
	chmod 0755 "${TARGET_DIR}/etc/init.d/S90vcos"
fi
if [ -f "${TARGET_DIR}/usr/bin/vcos-doctor" ]; then
	chmod 0755 "${TARGET_DIR}/usr/bin/vcos-doctor"
fi

cat > "${TARGET_DIR}/etc/vcos-release" <<EOF
VCOS_VERSION=0.2.0
VCOS_ABI=0.2
VCOS_KIND=buildroot
EOF
