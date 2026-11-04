#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
KEY=${VCOS_SIGNING_KEY:-"$ROOT/keys/private/vcos-signing-private.pem"}
PUBLIC=${VCOS_SIGNING_PUBKEY:-"$ROOT/keys/vcos-signing-public.pem"}

if [ "$#" -ne 1 ]; then
    echo "usage: sign-vmod.sh PACKAGE" >&2
    exit 2
fi

PACKAGE=$1
SIGNATURE="$PACKAGE.sig"

if [ ! -f "$PACKAGE" ]; then
    echo "sign-vmod: missing $PACKAGE" >&2
    exit 1
fi
if [ ! -f "$KEY" ]; then
    echo "sign-vmod: missing signing key $KEY" >&2
    exit 1
fi

DIGEST=$(mktemp)
trap 'rm -f "$DIGEST"' EXIT

openssl dgst -sha256 -binary "$PACKAGE" >"$DIGEST"
openssl pkeyutl -sign -rawin -inkey "$KEY" -in "$DIGEST" -out "$SIGNATURE"

if [ -f "$PUBLIC" ]; then
    if ! openssl pkeyutl -verify -rawin -pubin -inkey "$PUBLIC" \
        -in "$DIGEST" -sigfile "$SIGNATURE" >/dev/null; then
        rm -f "$SIGNATURE"
        echo "sign-vmod: signing key does not match $PUBLIC" >&2
        exit 1
    fi
fi

echo "$SIGNATURE"
