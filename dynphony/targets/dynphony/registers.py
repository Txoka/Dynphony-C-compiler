"""Architectural Dynphony register names."""

from enum import IntEnum


class Register(IntEnum):
    ZR = 0
    R1 = 1
    R2 = 2
    R3 = 3
    R4 = 4
    R5 = 5
    R6 = 6
    R7 = 7
    R8 = 8
    R9 = 9
    R10 = 10
    R11 = 11
    R12 = 12
    R13 = 13
    SP = 14
    FLAGS = 15


REGISTER_NAMES = {
    register: (
        "zr"
        if register == Register.ZR
        else "sp"
        if register == Register.SP
        else "flags"
        if register == Register.FLAGS
        else register.name.lower()
    )
    for register in Register
}
REGISTERS_BY_NAME = {name: register for register, name in REGISTER_NAMES.items()}


def register_name(register: int | Register) -> str:
    """Return the assembler spelling for an architectural register."""
    return REGISTER_NAMES[Register(register)]


def parse_register(name: str) -> Register:
    """Parse an assembler register spelling."""
    try:
        return REGISTERS_BY_NAME[name.lower()]
    except KeyError as exc:
        raise ValueError(f"unknown Dynphony register: {name}") from exc


__all__ = [
    "REGISTER_NAMES",
    "REGISTERS_BY_NAME",
    "Register",
    "parse_register",
    "register_name",
]
