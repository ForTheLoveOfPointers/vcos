#!/usr/bin/env python3
"""Wrap a flat PRG binary into a minimal iNES ROM."""

from __future__ import annotations

import argparse
import os
import sys

from font8x8 import build_chr


def build_ines(prg: bytes, chr_data: bytes | None = None, mirroring: int = 1,
               mapper: int = 0, battery: bool = False) -> bytes:
    if not prg:
        raise ValueError("empty PRG")
    chr_data = build_chr() if chr_data is None else chr_data
    if len(chr_data) == 0:
        chr_data = b""

    prg_size = ((len(prg) + 0x3FFF) // 0x4000) * 0x4000
    prg = prg.ljust(prg_size, b"\x00")
    chr_size = ((len(chr_data) + 0x1FFF) // 0x2000) * 0x2000 if chr_data else 0
    if chr_size:
        chr_data = chr_data.ljust(chr_size, b"\x00")

    prg_banks = prg_size // 0x4000
    chr_banks = chr_size // 0x2000
    if prg_banks > 255 or chr_banks > 255:
        raise ValueError("ROM too large for iNES 1.0")

    flags6 = ((mapper & 0x0F) << 4) | (0x01 if mirroring else 0x00)
    if battery:
        flags6 |= 0x02
    flags7 = mapper & 0xF0
    header = bytes([0x4E, 0x45, 0x53, 0x1A, prg_banks, chr_banks,
                    flags6, flags7, 0, 0, 0, 0, 0, 0, 0, 0])
    return header + prg + chr_data


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("prg")
    parser.add_argument("-o", "--output")
    parser.add_argument("--chr")
    parser.add_argument("--mirroring", choices=["h", "v"], default="v")
    args = parser.parse_args(argv[1:])

    with open(args.prg, "rb") as handle:
        prg = handle.read()
    chr_data = None
    if args.chr:
        with open(args.chr, "rb") as handle:
            chr_data = handle.read()
    rom = build_ines(prg, chr_data, 1 if args.mirroring == "v" else 0)
    output = args.output or os.path.splitext(args.prg)[0] + ".nes"
    with open(output, "wb") as handle:
        handle.write(rom)
    print(f"mknes: {args.prg} -> {output} ({len(rom)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
