"""Dynphony C ABI register roles and calling convention constants."""

from dataclasses import dataclass

from .registers import Register


@dataclass(frozen=True)
class CallingConvention:
    argument_registers: tuple[Register, ...]
    return_register: Register
    scratch_register: Register
    frame_pointer: Register
    pic_base_register: Register
    caller_saved: frozenset[Register]
    callee_saved: frozenset[Register]


ABI = CallingConvention(
    argument_registers=(
        Register.R1,
        Register.R2,
        Register.R3,
        Register.R4,
        Register.R5,
        Register.R6,
    ),
    return_register=Register.R1,
    scratch_register=Register.R7,
    frame_pointer=Register.R12,
    pic_base_register=Register.R13,
    caller_saved=frozenset(
        {
            Register.R1,
            Register.R2,
            Register.R3,
            Register.R4,
            Register.R5,
            Register.R6,
            Register.R7,
            Register.FLAGS,
        }
    ),
    callee_saved=frozenset(
        {
            Register.R8,
            Register.R9,
            Register.R10,
            Register.R11,
            Register.R12,
            Register.R13,
        }
    ),
)

__all__ = ["ABI", "CallingConvention"]
