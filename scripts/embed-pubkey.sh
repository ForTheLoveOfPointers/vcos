#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
PUBLIC=${1:-"$ROOT/keys/vcos-signing-public.pem"}
OUTPUT=${2:-"$ROOT/modman/src/vcos_pubkey.inc"}

if [ ! -f "$PUBLIC" ]; then
    echo "embed-pubkey: missing $PUBLIC" >&2
    exit 1
fi

DER=$(mktemp)
trap 'rm -f "$DER"' EXIT

openssl pkey -pubin -in "$PUBLIC" -outform DER -out "$DER"
{
    echo '#include <stddef.h>'
    xxd -i -n vcos_pubkey_der "$DER" |
        sed -e 's/^unsigned char/static const unsigned char/' \
            -e 's/^unsigned int/static const size_t/'
} >"$OUTPUT"

echo "$OUTPUT"
