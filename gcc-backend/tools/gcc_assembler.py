#!/usr/bin/env python3
"""Minimal two-pass assembler for the GCC 'moxie'-hijacked Dynphony backend's .s output."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from symphony.targets.symphony import isa
from symphony.targets.symphony.registers import parse_register, Register

COND_JUMP = {
    "e": "je", "ne": "jne", "b": "jb", "ae": "jae", "be": "jbe",
    "a": "ja", "l": "jl", "ge": "jge", "le": "jle", "g": "jg",
}


def pad_fixed_width(data):
    """Symphony pads every raw instruction to 4 bytes; mirrors Assembler.emit
    in symphony/targets/symphony/assembler.py so hand-assembled GCC output
    lands on the same instruction boundaries the native/reference emulators
    expect (both require fixed-width 4-byte slots for Symphony)."""
    data = bytes(data)
    out = bytearray()
    position = 0
    while position < len(data):
        opcode = data[position]
        if opcode in (0, 8):
            size = 1
        elif opcode in (1, 3, 5, 6, 7):
            size = 2
        elif opcode in (2, 4) or 0x20 <= opcode <= 0x77:
            size = 4 if opcode & 0x10 else 3
        elif opcode in (0x12, 0x14):
            size = 4
        else:
            raise ValueError(f"cannot pad unknown Symphony opcode {opcode:#x}")
        instruction = data[position:position + size]
        if len(instruction) != size:
            raise ValueError("truncated instruction while padding Symphony")
        out.extend(instruction)
        out.extend(bytes(4 - size))
        position += size
    return bytes(out)


def namespace_local_labels(text, prefix):
    """GCC's local labels (.Lnn control-flow labels, .LCnn constant-pool
    labels) are only unique within one compilation unit; concatenating
    multiple .s files (e.g. a hand-written runtime plus a compiled program)
    can silently collide two same-numbered labels from different functions,
    corrupting control flow with no assembler error. Rewrite every .L...
    local label (definition or reference) to .{prefix}_L... so files can be
    safely concatenated."""
    return re.sub(r"\.L([A-Za-z]*\d+)\b", rf".{prefix}_L\1", text)


def parse_operand(tok):
    tok = tok.strip()
    try:
        return parse_register(tok)
    except ValueError:
        pass
    if re.fullmatch(r"-?\d+", tok):
        return int(tok)
    return tok  # symbol


class Assembler:
    def __init__(self):
        self.labels = {}
        self.instrs = []  # list of (label_or_None, mnemonic, operands, size_bytes)

    def parse_line(self, line):
        line = line.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            return None
        m = re.match(r"^([A-Za-z_.$][\w.$]*):\s*$", line)
        if m:
            return ("label", m.group(1))
        if line.startswith("."):
            return ("directive", line)
        parts = line.split(None, 1)
        mnem = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        operands = [o.strip() for o in rest.split(",")] if rest else []
        return ("insn", mnem, operands)

    def size_of(self, mnem, operands):
        # Padding only depends on each raw sub-instruction's opcode byte, not
        # on the actual resolved value of any symbol/immediate, so a
        # placeholder (unresolved symbols -> 0) encode+pad gives the true
        # final length even before labels are known.
        return len(pad_fixed_width(self.encode_insn(mnem, operands, placeholder=True)))

    @staticmethod
    def parse_directive(line):
        parts = line.split(None, 1)
        name = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        args = [a.strip() for a in rest.split(",")] if rest else []
        return name, args

    def directive_size(self, name, args, addr):
        if name == ".byte":
            return len(args)
        if name in (".2byte", ".short", ".hword"):
            return 2 * len(args)
        if name in (".4byte", ".long", ".word"):
            return 4 * len(args)
        if name in (".zero", ".skip"):
            return int(args[0], 0)
        if name in (".align", ".p2align", ".balign"):
            align = int(args[0], 0)
            if name == ".p2align":
                align = 1 << align
            pad = (-addr) % align
            return pad
        return 0

    def load(self, text):
        addr = 0
        self.data = []  # list of (addr, name, args)
        for raw in text.splitlines():
            parsed = self.parse_line(raw)
            if parsed is None:
                continue
            if parsed[0] == "directive":
                name, args = self.parse_directive(parsed[1])
                size = self.directive_size(name, args, addr)
                if size:
                    self.data.append((addr, name, args))
                    addr += size
                continue
            if parsed[0] == "label":
                self.labels[parsed[1]] = addr
                continue
            _, mnem, operands = parsed
            size = self.size_of(mnem, operands)
            self.instrs.append((addr, mnem, operands))
            addr += size
        self.total_size = addr

    def resolve(self, tok, placeholder=False):
        v = parse_operand(tok)
        if isinstance(v, str):
            if v not in self.labels:
                if placeholder:
                    return 0
                raise ValueError(f"undefined symbol: {v}")
            return self.labels[v]
        return v

    def encode_directive(self, name, args):
        if name == ".byte":
            return bytes(self.resolve(a) & 0xFF for a in args)
        if name in (".2byte", ".short", ".hword"):
            out = bytearray()
            for a in args:
                v = self.resolve(a) & 0xFFFF
                out += v.to_bytes(2, "little")
            return bytes(out)
        if name in (".4byte", ".long", ".word"):
            out = bytearray()
            for a in args:
                v = self.resolve(a) & 0xFFFFFFFF
                out += v.to_bytes(4, "little")
            return bytes(out)
        if name in (".zero", ".skip"):
            return bytes(int(args[0], 0))
        if name in (".align", ".p2align", ".balign"):
            return b""  # padding length computed at load() time; emitted separately
        return b""

    def encode(self):
        chunks = [(addr, "insn", mnem, operands) for addr, mnem, operands in self.instrs]
        chunks += [(addr, "data", name, args) for addr, name, args in self.data]
        chunks.sort(key=lambda c: c[0])

        out = bytearray()
        for addr, kind, a, b in chunks:
            if len(out) < addr:
                out += bytes(addr - len(out))
            if kind == "insn":
                out += pad_fixed_width(self.encode_insn(a, b))
            else:
                encoded = self.encode_directive(a, b)
                expected = self.directive_size(a, b, addr)
                if len(encoded) < expected:
                    encoded += bytes(expected - len(encoded))
                out += encoded
        if len(out) < self.total_size:
            out += bytes(self.total_size - len(out))
        assert len(out) == self.total_size, (len(out), self.total_size)
        return bytes(out)

    def encode_insn(self, mnem, operands, placeholder=False):
        ops = [parse_operand(o) for o in operands]

        def rv(o):
            if isinstance(o, Register):
                return o
            raise ValueError(f"expected register, got {o!r}")

        def iv(o):
            if isinstance(o, int):
                return o & 0xFFFFFFFF
            if isinstance(o, str):
                return self.resolve(o, placeholder=placeholder)
            raise ValueError(f"expected immediate/symbol, got {o!r}")

        alu_imm_map = {
            "addi": "add", "subi": "sub", "andi": "and", "ori": "or",
            "xori": "xor", "lsli": "lsl", "lsri": "lsr", "asri": "asr",
        }
        alu_reg_map = {
            "add": "add", "sub": "sub", "and": "and", "or": "or", "xor": "xor",
            "lsl": "lsl", "lsr": "lsr", "asr": "asr",
        }

        if mnem == "mov":
            d, s = ops
            if isinstance(s, Register):
                return isa.mov(rv(d), rv(s))
            imm = iv(s)
            if imm < 0:
                imm &= 0xFFFF
            return isa.mov(rv(d), imm, True)

        if mnem == "movabs":
            d, s = ops
            val = iv(s)
            return isa.constant(rv(d), val)

        if mnem == "cmp":
            a, b = ops
            if isinstance(b, Register):
                return isa.alu("cmp", Register.FLAGS, rv(a), rv(b), False)
            imm = iv(b)
            if imm < 0:
                imm &= 0xFFFF
            return isa.alu("cmp", Register.FLAGS, rv(a), imm, True)

        if mnem in alu_imm_map:
            base = alu_imm_map[mnem]
            d, a, b = ops
            imm = iv(b)
            if imm < 0:
                imm &= 0xFFFF
            return isa.alu(base, rv(d), rv(a), imm, True)

        if mnem in alu_reg_map:
            base = alu_reg_map[mnem]
            d, a, b = ops
            return isa.alu(base, rv(d), rv(a), rv(b), False)

        if mnem == "nop":
            return isa.mov(Register.ZR, Register.ZR)

        if mnem == "jmp":
            (t,) = ops
            if isinstance(t, Register):
                return isa.jump("jmp", rv(t))
            return isa.jump("jmp", iv(t), True)

        if mnem.startswith("j") and mnem[1:] in COND_JUMP:
            (t,) = ops
            name = COND_JUMP[mnem[1:]]
            if isinstance(t, Register):
                return isa.jump(name, rv(t))
            return isa.jump(name, iv(t), True)

        if mnem == "call":
            (t,) = ops
            # isa.call()'s default return_offset (16) assumes Dynphony's
            # variable-width encoding (counter=2B, add=4B, push=7B, jmp=3B).
            # Symphony pads every one of those four sub-instructions to 4
            # bytes (see pad_fixed_width / symphony/targets/symphony/
            # assembler.py's identical padding), so the real byte distance
            # from counter's own PC to the address right after the call
            # sequence is 4+4+(4+4)+4 = 20, not 16 -- matching how the real
            # backend overrides link_call's return_offset for
            # fixed_instruction_width targets.
            return isa.call(rv(t), return_offset=20)

        if mnem == "ret":
            return isa.ret()

        if mnem == "in":
            (d,) = ops
            return isa.input_(rv(d))

        if mnem == "out":
            (v,) = ops
            return isa.output(rv(v))

        if mnem in ("load32", "load16", "load8"):
            size = {"load32": 4, "load16": 2, "load8": 1}[mnem]
            d, a = ops
            return isa.load(size, rv(d), rv(a))

        if mnem in ("store32", "store16", "store8"):
            size = {"store32": 4, "store16": 2, "store8": 1}[mnem]
            v, a = ops
            return isa.store(size, rv(a), rv(v))

        raise ValueError(f"unknown mnemonic: {mnem} {operands}")


def assemble(text, org_labels=None):
    asm = Assembler()
    if org_labels:
        asm.labels.update(org_labels)
    asm.load(text)
    return asm


if __name__ == "__main__":
    path = sys.argv[1]
    with open(path) as f:
        text = f.read()
    asm = assemble(text)
    code = asm.encode()
    print(f"assembled {len(code)} bytes")
    for name, addr in sorted(asm.labels.items(), key=lambda kv: kv[1]):
        print(f"  {addr:6d}  {name}")
