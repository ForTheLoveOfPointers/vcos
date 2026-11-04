#!/usr/bin/env python3
"""Build the guest shell, demo and music cartridge images."""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from font8x8 import build_chr
from mknes import build_ines
from vcasm import assemble_file

PROGRAMS = ["shell", "demo", "music"]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="out")
    parser.add_argument("--src", default=os.path.join(os.path.dirname(__file__), "..", "src"))
    args = parser.parse_args(argv[1:])

    out_dir = os.path.abspath(args.out)
    src_dir = os.path.abspath(args.src)
    os.makedirs(out_dir, exist_ok=True)
    chr_data = build_chr()

    built = []
    for name in PROGRAMS:
        source = os.path.join(src_dir, name + ".asm")
        if not os.path.isfile(source):
            print(f"build_roms: missing {source}", file=sys.stderr)
            return 1
        prg = assemble_file(source, [src_dir])
        rom = build_ines(prg, chr_data, mirroring=1, mapper=0)
        rom_path = os.path.join(out_dir, name + ".nes")
        with open(rom_path, "wb") as handle:
            handle.write(rom)
        prg_path = os.path.join(out_dir, name + ".prg")
        with open(prg_path, "wb") as handle:
            handle.write(prg)
        built.append(rom_path)
        print(f"build_roms: {name}: {len(rom)} byte cartridge -> {rom_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
