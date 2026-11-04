#!/usr/bin/env python3
import argparse
import hashlib
import io
import json
import os
import sys
import tarfile

FORMAT_VERSION = 1
ABI_VERSION = 1
ARCH = "x86_64"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tar_info(name, mode, size):
    info = tarfile.TarInfo(name)
    info.mode = mode
    info.size = size
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    return info


def add_bytes(tar, name, data, mode):
    info = tar_info(name, mode, len(data))
    tar.addfile(info, io.BytesIO(data))


def add_file(tar, name, path, mode):
    info = tar_info(name, mode, os.path.getsize(path))
    with open(path, "rb") as f:
        tar.addfile(info, f)


def main():
    parser = argparse.ArgumentParser(description="create an unsigned .vmod package")
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--module", required=True, help="module .so")
    parser.add_argument("--out", required=True)
    parser.add_argument("--system", action="append", default=[])
    parser.add_argument("--extension", action="append", default=[])
    parser.add_argument("--license", default="MIT")
    parser.add_argument("--license-file")
    parser.add_argument("--description", default="")
    parser.add_argument("--asset", action="append", default=[])
    parser.add_argument("--abi-version", type=int, default=ABI_VERSION)
    args = parser.parse_args()

    if not os.path.isfile(args.module):
        raise SystemExit(f"make-vmod: missing module {args.module}")

    entries = []

    def entry(archive_path, host_path, mode):
        entries.append((archive_path, host_path, mode))

    entry("payload/module.so", args.module, 0o755)
    for asset in args.asset:
        if not os.path.isfile(asset):
            raise SystemExit(f"make-vmod: missing asset {asset}")
        name = os.path.basename(asset)
        archive_path = f"payload/assets/{name}"
        if any(e[0] == archive_path for e in entries):
            raise SystemExit(f"make-vmod: duplicate asset name {name}")
        entry(archive_path, asset, 0o644)
    if args.license_file:
        if not os.path.isfile(args.license_file):
            raise SystemExit(f"make-vmod: missing license {args.license_file}")
        entry("licenses/LICENSE", args.license_file, 0o644)

    files = {}
    for archive_path, host_path, _ in entries:
        if len(archive_path.encode("utf-8")) >= 100:
            raise SystemExit(f"make-vmod: archive path too long: {archive_path}")
        files[archive_path] = sha256_file(host_path)

    manifest = {
        "format": FORMAT_VERSION,
        "abi_version": args.abi_version,
        "id": args.id,
        "name": args.name,
        "version": args.version,
        "architecture": ARCH,
        "entrypoint": "payload/module.so",
        "license": args.license,
        "systems": args.system,
        "extensions": args.extension,
        "files": files,
    }
    if args.description:
        manifest["description"] = args.description

    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    temp = args.out + ".tmp"
    with tarfile.open(temp, "w", format=tarfile.USTAR_FORMAT) as tar:
        add_bytes(tar, "manifest.json", manifest_bytes, 0o644)
        for archive_path, host_path, mode in entries:
            add_file(tar, archive_path, host_path, mode)
    os.replace(temp, args.out)
    try:
        os.unlink(args.out + ".sig")
    except FileNotFoundError:
        pass
    print(args.out)


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(1)
