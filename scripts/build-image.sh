#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BR="$ROOT/buildroot/buildroot"
EXT="$ROOT/buildroot/br2-external"
OUT="$ROOT/buildroot/output"

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1700000000}"
export TZ=UTC

if [ ! -d "$BR" ]; then
    "$ROOT/scripts/get-buildroot.sh"
fi

make -C "$BR" BR2_EXTERNAL="$EXT" O="$OUT" vcos_qemu_x86_64_defconfig
make -C "$BR" BR2_EXTERNAL="$EXT" O="$OUT" -j"$(nproc)"

(
    cd "$OUT/images"
    sha256sum vcos.img bzImage rootfs.ext2 vcos-data.ext4 > SHA256SUMS
)

echo
echo "build-image: artifacts in $OUT/images"
ls -lh "$OUT/images" 2>/dev/null || true
echo
echo "build-image: checksums"
cat "$OUT/images/SHA256SUMS"
