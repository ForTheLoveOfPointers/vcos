#!/usr/bin/env python3
"""Small two-pass 6502 assembler for VCOS guest programs.

Only the official NMOS 6502 instruction set is supported. The assembler
emits a flat binary starting at the first .org (normally $8000).
"""

from __future__ import annotations

import os
import re
import sys

MODES = {
    "imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2,
    "abs": 3, "absx": 3, "absy": 3, "ind": 3, "indx": 2, "indy": 2,
    "rel": 2,
}

OPS = {
    "ADC": {"imm": 0x69, "zp": 0x65, "zpx": 0x75, "abs": 0x6D, "absx": 0x7D, "absy": 0x79, "indx": 0x61, "indy": 0x71},
    "AND": {"imm": 0x29, "zp": 0x25, "zpx": 0x35, "abs": 0x2D, "absx": 0x3D, "absy": 0x39, "indx": 0x21, "indy": 0x31},
    "ASL": {"acc": 0x0A, "zp": 0x06, "zpx": 0x16, "abs": 0x0E, "absx": 0x1E},
    "BCC": {"rel": 0x90}, "BCS": {"rel": 0xB0}, "BEQ": {"rel": 0xF0},
    "BMI": {"rel": 0x30}, "BNE": {"rel": 0xD0}, "BPL": {"rel": 0x10},
    "BVC": {"rel": 0x50}, "BVS": {"rel": 0x70},
    "BIT": {"zp": 0x24, "abs": 0x2C},
    "BRK": {"imp": 0x00},
    "CLC": {"imp": 0x18}, "CLD": {"imp": 0xD8}, "CLI": {"imp": 0x58},
    "CLV": {"imp": 0xB8},
    "CMP": {"imm": 0xC9, "zp": 0xC5, "zpx": 0xD5, "abs": 0xCD, "absx": 0xDD, "absy": 0xD9, "indx": 0xC1, "indy": 0xD1},
    "CPX": {"imm": 0xE0, "zp": 0xE4, "abs": 0xEC},
    "CPY": {"imm": 0xC0, "zp": 0xC4, "abs": 0xCC},
    "DEC": {"zp": 0xC6, "zpx": 0xD6, "abs": 0xCE, "absx": 0xDE},
    "DEX": {"imp": 0xCA}, "DEY": {"imp": 0x88},
    "EOR": {"imm": 0x49, "zp": 0x45, "zpx": 0x55, "abs": 0x4D, "absx": 0x5D, "absy": 0x59, "indx": 0x41, "indy": 0x51},
    "INC": {"zp": 0xE6, "zpx": 0xF6, "abs": 0xEE, "absx": 0xFE},
    "INX": {"imp": 0xE8}, "INY": {"imp": 0xC8},
    "JMP": {"abs": 0x4C, "ind": 0x6C},
    "JSR": {"abs": 0x20},
    "LDA": {"imm": 0xA9, "zp": 0xA5, "zpx": 0xB5, "abs": 0xAD, "absx": 0xBD, "absy": 0xB9, "indx": 0xA1, "indy": 0xB1},
    "LDX": {"imm": 0xA2, "zp": 0xA6, "zpy": 0xB6, "abs": 0xAE, "absy": 0xBE},
    "LDY": {"imm": 0xA0, "zp": 0xA4, "zpx": 0xB4, "abs": 0xAC, "absx": 0xBC},
    "LSR": {"acc": 0x4A, "zp": 0x46, "zpx": 0x56, "abs": 0x4E, "absx": 0x5E},
    "NOP": {"imp": 0xEA},
    "ORA": {"imm": 0x09, "zp": 0x05, "zpx": 0x15, "abs": 0x0D, "absx": 0x1D, "absy": 0x19, "indx": 0x01, "indy": 0x11},
    "PHA": {"imp": 0x48}, "PHP": {"imp": 0x08},
    "PLA": {"imp": 0x68}, "PLP": {"imp": 0x28},
    "ROL": {"acc": 0x2A, "zp": 0x26, "zpx": 0x36, "abs": 0x2E, "absx": 0x3E},
    "ROR": {"acc": 0x6A, "zp": 0x66, "zpx": 0x76, "abs": 0x6E, "absx": 0x7E},
    "RTI": {"imp": 0x40}, "RTS": {"imp": 0x60},
    "SBC": {"imm": 0xE9, "zp": 0xE5, "zpx": 0xF5, "abs": 0xED, "absx": 0xFD, "absy": 0xF9, "indx": 0xE1, "indy": 0xF1},
    "SEC": {"imp": 0x38}, "SED": {"imp": 0xF8}, "SEI": {"imp": 0x78},
    "STA": {"zp": 0x85, "zpx": 0x95, "abs": 0x8D, "absx": 0x9D, "absy": 0x99, "indx": 0x81, "indy": 0x91},
    "STX": {"zp": 0x86, "zpy": 0x96, "abs": 0x8E},
    "STY": {"zp": 0x84, "zpx": 0x94, "abs": 0x8C},
    "TAX": {"imp": 0xAA}, "TAY": {"imp": 0xA8}, "TSX": {"imp": 0xBA},
    "TXA": {"imp": 0x8A}, "TXS": {"imp": 0x9A}, "TYA": {"imp": 0x98},
}

BRANCHES = {"BCC", "BCS", "BEQ", "BMI", "BNE", "BPL", "BVC", "BVS"}

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_.]*):\s*(.*)$")
SYMBOL_RE = re.compile(r"(?<![\w.])[A-Za-z_][A-Za-z0-9_.]*")
HEX_RE = re.compile(r"\$([0-9A-Fa-f]+)")
BIN_RE = re.compile(r"%([01]+)")
CHAR_RE = re.compile(r"'(\\.|[^'])'")


class AsmError(Exception):
    pass


def strip_comment(line: str) -> str:
    out = []
    in_str = False
    for ch in line:
        if ch == '"':
            in_str = not in_str
        if ch == ";" and not in_str:
            break
        out.append(ch)
    return "".join(out).rstrip()


def split_args(text: str) -> list[str]:
    args = []
    current = []
    in_str = False
    depth = 0
    for ch in text:
        if ch == '"':
            in_str = not in_str
            current.append(ch)
        elif ch == "(" and not in_str:
            depth += 1
            current.append(ch)
        elif ch == ")" and not in_str:
            depth -= 1
            current.append(ch)
        elif ch == "," and not in_str and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if current or text.strip():
        args.append("".join(current).strip())
    return args


def eval_expr(expr: str, symbols: dict[str, int]) -> int:
    e = expr.strip()
    if not e:
        raise AsmError("empty expression")
    low = high = False
    if e.startswith("<"):
        low = True
        e = e[1:].strip()
    elif e.startswith(">"):
        high = True
        e = e[1:].strip()

    e = CHAR_RE.sub(lambda m: str(ord(m.group(1)[-1])), e)
    e = HEX_RE.sub(lambda m: "0x" + m.group(1), e)
    e = BIN_RE.sub(lambda m: "0b" + m.group(1), e)

    def repl(match: re.Match[str]) -> str:
        name = match.group(0)
        if name not in symbols:
            raise AsmError(f"undefined symbol: {name}")
        return str(symbols[name])

    e = SYMBOL_RE.sub(repl, e)
    try:
        value = eval(e, {"__builtins__": {}}, {})
    except Exception as exc:
        raise AsmError(f"bad expression {expr!r}: {exc}") from exc
    value = int(value)
    if low:
        value &= 0xFF
    if high:
        value = (value >> 8) & 0xFF
    return value


def try_eval(expr: str, symbols: dict[str, int]):
    try:
        return eval_expr(expr, symbols)
    except AsmError:
        return None


def classify(mnemonic: str, operand: str, symbols: dict[str, int]) -> str:
    table = OPS[mnemonic]
    op = operand.strip()
    if mnemonic in BRANCHES:
        return "rel"
    if op == "" or op.upper() == "A":
        return "acc" if "acc" in table else "imp"
    if op.startswith("#"):
        return "imm"
    if op.startswith("("):
        compact = op.replace(" ", "").upper()
        if compact.endswith(",X)"):
            return "indx"
        if compact.endswith("),Y"):
            return "indy"
        return "ind"
    match = re.match(r"^(.*),\s*([XxYy])$", op)
    if match:
        base = match.group(1).strip()
        index = match.group(2).upper()
        value = try_eval(base, symbols)
        modes = ("zpx", "absx") if index == "X" else ("zpy", "absy")
        if value is not None and 0 <= value <= 0xFF and modes[0] in table:
            return modes[0]
        if modes[1] in table:
            return modes[1]
        return modes[0]
    value = try_eval(op, symbols)
    if value is not None and 0 <= value <= 0xFF and "zp" in table:
        return "zp"
    return "abs"


def operand_expr(mode: str, operand: str) -> str:
    op = operand.strip()
    if mode == "imm":
        return op.lstrip("#").strip()
    if mode == "rel":
        return op
    if mode == "indx":
        inner = op[1:-1]
        return inner.rsplit(",", 1)[0].strip()
    if mode == "indy":
        close = op.rindex(")")
        return op[1:close].strip()
    if mode == "ind":
        return op[1:-1].strip()
    if mode in ("zpx", "absx", "zpy", "absy"):
        return op.rsplit(",", 1)[0].strip()
    return op


def expand_includes(path: str, include_dirs: list[str]) -> list[str]:
    base = os.path.dirname(os.path.abspath(path))
    lines: list[str] = []
    with open(path, "r", encoding="utf-8") as handle:
        raw = handle.readlines()
    for raw_line in raw:
        line = strip_comment(raw_line)
        stripped = line.strip()
        if stripped.lower().startswith(".include"):
            rest = stripped[len(".include"):].strip()
            name = rest.strip('"').strip("'")
            candidates = [name, os.path.join(base, name)]
            candidates += [os.path.join(d, name) for d in include_dirs]
            found = next((c for c in candidates if os.path.isfile(c)), None)
            if not found:
                raise AsmError(f"{path}: include not found: {name}")
            lines.extend(expand_includes(found, include_dirs))
        else:
            lines.append(line)
    return lines


def run_pass(lines: list[str], symbols: dict[str, int], emit: bool, origin_base: int) -> tuple[dict[int, int], int, int]:
    pc = origin_base
    start = None
    end = 0
    data: dict[int, int] = {}

    for line_no, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue

        match = LABEL_RE.match(line)
        if match:
            label, rest = match.group(1), match.group(2).strip()
            if label in symbols and emit is False:
                pass
            symbols[label] = pc
            line = rest
            if not line:
                continue

        if line.startswith("."):
            parts = line.split(None, 1)
            directive = parts[0].lower()
            arg = parts[1].strip() if len(parts) > 1 else ""

            if directive in (".org",):
                value = eval_expr(arg, symbols)
                if value < origin_base:
                    raise AsmError(f"line {line_no}: .org below ${origin_base:04X}")
                pc = value
            elif directive in (".equ", ".define", ".set"):
                args = split_args(arg)
                if len(args) != 2:
                    raise AsmError(f"line {line_no}: .equ needs NAME, VALUE")
                value = eval_expr(args[1], symbols)
                symbols[args[0]] = value
            elif directive in (".byte", ".db", ".byt"):
                values = []
                for token in split_args(arg):
                    if token.startswith('"') and token.endswith('"'):
                        values.extend(token[1:-1].encode("latin-1"))
                    elif emit:
                        values.append(eval_expr(token, symbols) & 0xFF)
                    else:
                        values.append(0)
                for value in values:
                    _put(data, pc, value, emit)
                    pc += 1
            elif directive in (".word", ".dw"):
                for token in split_args(arg):
                    if emit:
                        value = eval_expr(token, symbols) & 0xFFFF
                        _put(data, pc, value & 0xFF, emit)
                        _put(data, pc + 1, (value >> 8) & 0xFF, emit)
                    pc += 2
            elif directive in (".res", ".space", ".ds"):
                args = split_args(arg)
                count = eval_expr(args[0], symbols)
                fill = eval_expr(args[1], symbols) & 0xFF if len(args) > 1 else 0
                for _ in range(count):
                    _put(data, pc, fill, emit)
                    pc += 1
            else:
                raise AsmError(f"line {line_no}: unknown directive {directive}")
            start = pc if start is None else start
            end = max(end, pc)
            continue

        parts = line.split(None, 1)
        mnemonic = parts[0].upper()
        operand = parts[1].strip() if len(parts) > 1 else ""
        if mnemonic not in OPS:
            raise AsmError(f"line {line_no}: unknown mnemonic {mnemonic}")
        mode = classify(mnemonic, operand, symbols)
        table = OPS[mnemonic]
        if mode not in table:
            raise AsmError(f"line {line_no}: {mnemonic} does not support {mode}")
        opcode = table[mode]
        size = MODES[mode]

        if start is None:
            start = pc
        _put(data, pc, opcode, emit)
        pc += 1
        if size == 2:
            if mode == "rel":
                if emit:
                    target = eval_expr(operand, symbols)
                    offset = target - (pc + 1)
                    if not -128 <= offset <= 127:
                        raise AsmError(f"line {line_no}: branch out of range")
                    _put(data, pc, offset & 0xFF, emit)
            elif emit:
                value = eval_expr(operand_expr(mode, operand), symbols) & 0xFF
                _put(data, pc, value, emit)
            pc += 1
        elif size == 3:
            if emit:
                value = eval_expr(operand_expr(mode, operand), symbols) & 0xFFFF
                _put(data, pc, value & 0xFF, emit)
                _put(data, pc + 1, (value >> 8) & 0xFF, emit)
            pc += 2
        end = max(end, pc)

    if start is None:
        start = origin_base
        end = origin_base
    return data, start, end


def _put(data: dict[int, int], addr: int, value: int, emit: bool) -> None:
    if not emit:
        return
    if addr in data:
        raise AsmError(f"overlapping output at ${addr:04X}")
    data[addr] = value & 0xFF


def assemble_file(path: str, include_dirs: list[str] | None = None) -> bytes:
    include_dirs = include_dirs or []
    lines = expand_includes(path, include_dirs)

    symbols: dict[str, int] = {}
    origin_base = 0x8000
    _, start, end = run_pass(lines, symbols, False, origin_base)
    if start is None:
        raise AsmError("no output")
    data, start, end = run_pass(lines, symbols, True, origin_base)
    if start != origin_base:
        raise AsmError(f"output must start at ${origin_base:04X}, not ${start:04X}")

    out = bytearray([0] * (end - start))
    for addr, value in data.items():
        if addr < start or addr >= end:
            raise AsmError(f"address ${addr:04X} outside output range")
        out[addr - start] = value
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"usage: {argv[0]} SOURCE.asm [-o OUTPUT] [-I DIR]", file=sys.stderr)
        return 2
    output = None
    includes = []
    index = 1
    source = None
    while index < len(argv):
        arg = argv[index]
        if arg == "-o" and index + 1 < len(argv):
            output = argv[index + 1]
            index += 2
        elif arg == "-I" and index + 1 < len(argv):
            includes.append(argv[index + 1])
            index += 2
        else:
            source = arg
            index += 1
    if not source:
        print("no source", file=sys.stderr)
        return 2
    try:
        code = assemble_file(source, includes)
    except (AsmError, OSError) as exc:
        print(f"vcasm: {exc}", file=sys.stderr)
        return 1
    output = output or os.path.splitext(source)[0] + ".prg"
    with open(output, "wb") as handle:
        handle.write(code)
    print(f"vcasm: {source} -> {output} ({len(code)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
