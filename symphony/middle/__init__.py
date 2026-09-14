"""Language- and target-independent compiler representations."""

from .ir import FunctionIR, Instruction, ModuleIR, lower

__all__ = ["FunctionIR", "Instruction", "ModuleIR", "lower"]
