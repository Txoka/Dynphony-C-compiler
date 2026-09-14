"""Dynphony C ABI register roles and calling convention constants."""

from dataclasses import dataclass

from .registers import Register


@dataclass(frozen=True)
class CallingConvention:
    argument_registers: tuple[Register, ...]
    return_registers: tuple[Register, ...]
    status_register: Register
    link_register: Register
    call_target_register: Register
    scratch_register: Register
    frame_pointer: Register
    pic_base_register: Register
    caller_saved: frozenset[Register]
    callee_saved: frozenset[Register]

    @property
    def return_register(self):
        """The scalar C return register."""
        return self.return_registers[0]


ABI = CallingConvention(
    argument_registers=(
        Register.R1,
        Register.R2,
        Register.R3,
        Register.R4,
        Register.R5,
        Register.R6,
        Register.R7,
    ),
    return_registers=(
        Register.R1,
        Register.R2,
        Register.R3,
        Register.R4,
        Register.R5,
        Register.R6,
        Register.R7,
    ),
    status_register=Register.FLAGS,
    link_register=Register.R13,
    call_target_register=Register.FLAGS,
    scratch_register=Register.R7,
    frame_pointer=Register.R11,
    pic_base_register=Register.R12,
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
        }
    ),
)

__all__ = ["ABI", "CallingConvention"]
