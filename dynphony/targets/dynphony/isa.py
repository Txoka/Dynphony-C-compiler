"""Dynphony ISA names and encoding primitives. All immediates are U16."""

from .registers import (
    REGISTER_NAMES,
    REGISTERS_BY_NAME,
    Register,
    parse_register,
    register_name,
)

ALU = {
    "nand": 0x20,
    "or": 0x21,
    "and": 0x22,
    "nor": 0x23,
    "add": 0x24,
    "sub": 0x25,
    "xor": 0x26,
    "lsl": 0x27,
    "lsr": 0x28,
    "asr": 0x29,
    "cmp": 0x2A,
}
JUMP = {
    "jmp": 0x48,
    "je": 0x41,
    "jne": 0x49,
    "jb": 0x42,
    "jae": 0x4A,
    "jbe": 0x43,
    "ja": 0x4B,
    "jl": 0x44,
    "jge": 0x4C,
    "jle": 0x45,
    "jg": 0x4D,
}


def reg(r):
    if not isinstance(r, int) or not 0 <= r < 16:
        raise ValueError("register must be 0..15")
    return r


def u16(v):
    if not 0 <= v <= 65535:
        raise ValueError("immediate must be 0..65535")
    return v.to_bytes(2, "big")


def alu(op, d, a, b, immediate=False):
    reg(d)
    reg(a)
    return bytes([ALU[op] + (0x10 if immediate else 0), d * 16 + a]) + (
        u16(b) if immediate else bytes([reg(b)])
    )


def jump(op, target, immediate=False):
    return bytes([JUMP[op] + (0x10 if immediate else 0), 15]) + (
        u16(target) if immediate else bytes([reg(target)])
    )


def load(size, d, address):
    return bytes([0x60 + {1: 0, 2: 1, 4: 2}[size], reg(d) * 16, reg(address)])


def store(size, address, value):
    return bytes([0x64 + {1: 0, 2: 1, 4: 2}[size], reg(value), reg(address)])


def input_(d):
    return bytes([0x01, reg(d) * 16])


def output(value, immediate=False):
    return bytes([0x12 if immediate else 0x02, 0]) + (
        u16(value) if immediate else bytes([reg(value)])
    )


def keyboard(d):
    return bytes([0x03, reg(d) * 16])


def screen(setting, value, immediate=False):
    reg(setting)
    return bytes([0x14 if immediate else 0x04, setting]) + (
        u16(value) if immediate else bytes([reg(value)])
    )


def time(part, d):
    if part not in (0, 1):
        raise ValueError("time part must be 0 or 1")
    return bytes([0x05 + part, reg(d) * 16])


def persistent_load(d, address, immediate=False):
    reg(d)
    return bytes([0x73 if immediate else 0x63, d * 16]) + (
        u16(address) if immediate else bytes([reg(address)])
    )


def persistent_store(address, value, immediate=False):
    reg(value)
    return bytes([0x77 if immediate else 0x67, value]) + (
        u16(address) if immediate else bytes([reg(address)])
    )


def counter(d):
    return bytes([7, reg(d) * 16])


def mov(d, source, immediate=False):
    """ISA alias for ``or d, zr, source``; both spellings have identical bytes."""
    return alu("or", d, Register.ZR, source, immediate)


def push(r):
    return alu("sub", Register.SP, Register.SP, 4, True) + store(
        4, Register.SP, r
    )


def pop(r):
    return load(4, r, Register.SP) + alu(
        "add", Register.SP, Register.SP, 4, True
    )


def call(target, immediate=False, *, return_offset=None):
    if return_offset is None:
        return_offset = 17 if immediate else 16
    return (
        counter(Register.FLAGS)
        + alu(
            "add",
            Register.FLAGS,
            Register.FLAGS,
            return_offset,
            True,
        )
        + push(Register.FLAGS)
        + jump("jmp", target, immediate)
    )


def ret():
    return pop(Register.FLAGS) + jump("jmp", Register.FLAGS)


def constant(r, value):
    value &= 0xFFFFFFFF
    # Always 12 bytes: keeps label and address fixups independent of layout.
    return (
        mov(r, value >> 16, True)
        + alu("lsl", r, r, 16, True)
        + alu("or", r, r, value & 65535, True)
    )


def cheap_constant(r, value):
    """Shortest known materialization for a non-relocatable 32-bit constant."""
    value &= 0xFFFFFFFF
    if value <= 0xFFFF:
        return mov(r, value, True)
    magnitude = (-value) & 0xFFFFFFFF
    if magnitude <= 0xFFFF:
        return alu("sub", r, 0, magnitude, True)
    return constant(r, value)
