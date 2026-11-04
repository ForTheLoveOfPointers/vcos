#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
MODMAN="$ROOT/build/modman/vcos-modman"
HOST="$ROOT/build/module-host/vcos-module-host"
MODULE="$ROOT/build/modules/null.so"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail()
{
    echo "modman_tests: FAIL: $*" >&2
    exit 1
}

test -x "$MODMAN" || fail "missing $MODMAN"
test -x "$HOST" || fail "missing $HOST"
test -f "$MODULE" || fail "missing $MODULE"

PKG="$TMP/null.vmod"
python3 "$ROOT/scripts/make-vmod.py" \
    --id org.vcos.null \
    --name "VCOS Null" \
    --version 1.0.0 \
    --module "$MODULE" \
    --system null \
    --extension .null \
    --description "null test module" \
    --out "$PKG" >/dev/null

"$MODMAN" info "$PKG" >"$TMP/info"
grep -q '^id: org.vcos.null$' "$TMP/info" || fail "info id"
grep -q '^abi_version: 1$' "$TMP/info" || fail "info abi"
grep -q 'payload/module.so' "$TMP/info" || fail "info entrypoint"

if "$MODMAN" install "$PKG" --root "$TMP/data" >"$TMP/noallow.out" 2>"$TMP/noallow.err"; then
    fail "install accepted an unsigned package without --allow-unsigned"
fi
grep -q 'allow-unsigned' "$TMP/noallow.err" || fail "missing unsigned warning"

SIGN_KEY="${VCOS_SIGNING_KEY:-$ROOT/keys/private/vcos-signing-private.pem}"
if [ -f "$SIGN_KEY" ]; then
    VCOS_SIGNING_KEY="$SIGN_KEY" "$ROOT/scripts/sign-vmod.sh" "$PKG" >/dev/null
    "$MODMAN" info "$PKG" >"$TMP/signed-info"
    grep -q '^signature: valid$' "$TMP/signed-info" || fail "signed info"
    "$MODMAN" install "$PKG" --root "$TMP/signed-data"         >"$TMP/signed.out" 2>"$TMP/signed.err"
    grep -q 'signature verified' "$TMP/signed.err" ||
        fail "signed install did not report verification"
    test -f "$TMP/signed-data/modules/org.vcos.null/1.0.0/payload/module.so" ||
        fail "signed module not installed"

    cp "$PKG" "$TMP/tampered.vmod"
    cp "$PKG.sig" "$TMP/tampered.vmod.sig"
    printf 'x' >>"$TMP/tampered.vmod"
    if "$MODMAN" install "$TMP/tampered.vmod" --root "$TMP/tampered-data"         --allow-unsigned >"$TMP/tampered.out" 2>"$TMP/tampered.err"; then
        fail "install accepted a tampered signed package"
    fi
    grep -q 'signature verification failed' "$TMP/tampered.err" ||
        fail "missing tampered signature error"

    cp "$PKG" "$TMP/badsig.vmod"
    cp "$PKG.sig" "$TMP/badsig.vmod.sig"
    printf 'X' | dd of="$TMP/badsig.vmod.sig" bs=1 seek=0 conv=notrunc         status=none
    if "$MODMAN" install "$TMP/badsig.vmod" --root "$TMP/badsig-data"         --allow-unsigned >"$TMP/badsig.out" 2>"$TMP/badsig.err"; then
        fail "install accepted an invalid signature"
    fi
    grep -q 'signature verification failed' "$TMP/badsig.err" ||
        fail "missing invalid signature error"
fi

"$MODMAN" install "$PKG" --root "$TMP/data" --allow-unsigned >"$TMP/install.out"
test -f "$TMP/data/modules/org.vcos.null/1.0.0/payload/module.so" ||
    fail "module not installed"
test -L "$TMP/data/modules/org.vcos.null/current" || fail "current not a symlink"
test "$(readlink "$TMP/data/modules/org.vcos.null/current")" = "1.0.0" ||
    fail "current points to the wrong version"

"$MODMAN" list --root "$TMP/data" >"$TMP/list"
grep -q '^org.vcos.null 1.0.0 VCOS Null null$' "$TMP/list" ||
    fail "list output"

if "$MODMAN" install "$PKG" --root "$TMP/data" --allow-unsigned \
    >"$TMP/reinstall.out" 2>"$TMP/reinstall.err"; then
    fail "reinstall of the same version succeeded"
fi
grep -q 'already installed' "$TMP/reinstall.err" || fail "missing reinstall error"

VCOS_MODULE_HOST="$HOST" "$MODMAN" run org.vcos.null "$ROOT/README.md" \
    --root "$TMP/data" --backend ppm --frames 1 \
    --screenshot "$TMP/module.ppm" --audio none --no-pace --no-kmsg
test -s "$TMP/module.ppm" || fail "module run produced no frame"
grep -q 'module=org.vcos.null' "$TMP/data/logs/module-host.log" ||
    fail "module run did not load the installed module"

python3 - "$PKG" "$TMP/badhash.vmod" <<'PY'
import io
import json
import sys
import tarfile

source, target = sys.argv[1:3]
with tarfile.open(source, "r") as src:
    entries = [(m, src.extractfile(m).read()) for m in src.getmembers()]
manifest = json.loads(next(data for m, data in entries if m.name == "manifest.json"))
manifest["files"]["payload/module.so"] = "0" * 64
with tarfile.open(target, "w", format=tarfile.USTAR_FORMAT) as dst:
    for member, data in entries:
        if member.name == "manifest.json":
            data = json.dumps(manifest, indent=2, sort_keys=True).encode() + b"\n"
            member.size = len(data)
        dst.addfile(member, io.BytesIO(data))
PY
if "$MODMAN" install "$TMP/badhash.vmod" --root "$TMP/data" --allow-unsigned \
    >"$TMP/badhash.out" 2>"$TMP/badhash.err"; then
    fail "install accepted a bad SHA-256"
fi
grep -q 'SHA-256 mismatch' "$TMP/badhash.err" || fail "missing hash error"

python3 - "$TMP/escape.vmod" <<'PY'
import io
import sys
import tarfile

with tarfile.open(sys.argv[1], "w", format=tarfile.USTAR_FORMAT) as tar:
    info = tarfile.TarInfo("../escape")
    info.size = 1
    tar.addfile(info, io.BytesIO(b"x"))
PY
if "$MODMAN" install "$TMP/escape.vmod" --root "$TMP/data" --allow-unsigned \
    >"$TMP/escape.out" 2>"$TMP/escape.err"; then
    fail "install accepted a path traversal entry"
fi
test ! -e "$TMP/data/escape" || fail "path traversal escaped the staging directory"

"$MODMAN" remove org.vcos.null --root "$TMP/data" >/dev/null
test ! -e "$TMP/data/modules/org.vcos.null" || fail "remove left the module"

"$MODMAN" list --root "$TMP/empty" >/dev/null

TARGET="$ROOT/buildroot/output/target"
TARGET_MODMAN="$TARGET/usr/bin/vcos-modman"
if command -v qemu-x86_64 >/dev/null 2>&1 && test -x "$TARGET_MODMAN"; then
    qemu-x86_64 -L "$TARGET" "$TARGET_MODMAN" info "$PKG" >"$TMP/target-info"
    grep -q '^id: org.vcos.null$' "$TMP/target-info" ||
        fail "target modman info"
    qemu-x86_64 -L "$TARGET" "$TARGET_MODMAN" install "$PKG" \
        --root "$TMP/target-data" --allow-unsigned >/dev/null
    qemu-x86_64 -L "$TARGET" "$TARGET_MODMAN" list \
        --root "$TMP/target-data" >"$TMP/target-list"
    grep -q '^org.vcos.null 1.0.0 VCOS Null null$' "$TMP/target-list" ||
        fail "target modman list"
fi

echo "modman_tests: ok"
