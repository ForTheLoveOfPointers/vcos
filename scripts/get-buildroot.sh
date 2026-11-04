#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/buildroot/buildroot"
REF="${1:-2026.08}"

if [ -d "$DEST/.git" ]; then
    echo "get-buildroot: already present at $DEST"
    exit 0
fi

mkdir -p "$ROOT/buildroot"
echo "get-buildroot: cloning Buildroot $REF into $DEST"
git clone --depth 1 --branch "$REF" \
    https://gitlab.com/buildroot.org/buildroot.git "$DEST"
