"""Symbolic Dynphony assembly, relocation, and final control relaxation."""

from ...middle.model import CompileError
from . import isa
from .abi import ABI


class Assembler:
    def __init__(self, target):
        self.target = target
        self.code = bytearray()
        self.labels = {}
        self.fixups = []
        self.control_fixups = []

    def emit(self, data):
        data = bytes(data)
        if not self.target.fixed_instruction_width:
            self.code.extend(data)
            return
        position = 0
        while position < len(data):
            opcode = data[position]
            if opcode in (0, 8):
                size = 1
            elif opcode in (1, 3, 5, 6, 7):
                size = 2
            elif opcode in (2, 4) or 0x20 <= opcode <= 0x77:
                size = 4 if opcode & 0x10 else 3
            elif opcode == 0x12 or opcode == 0x14:
                size = 4
            else:
                raise CompileError(f"cannot pad unknown Symphony opcode {opcode:#x}")
            instruction = data[position:position + size]
            if len(instruction) != size:
                raise CompileError("truncated instruction while emitting Symphony")
            self.code.extend(instruction)
            self.code.extend(bytes(4 - size))
            position += size

    def emit_data(self, data):
        self.code.extend(data)

    def _call_bytes(self, target, immediate=False):
        return isa.call(
            target,
            immediate,
            return_offset=20 if self.target.fixed_instruction_width else None,
        )

    def call_register(self, register):
        self.emit(self._call_bytes(register))

    def label(self, name):
        if name in self.labels:
            raise CompileError(f"duplicate backend symbol {name}")
        self.labels[name] = len(self.code)

    def address(self, register, label, addend=0):
        self.fixups.append((len(self.code), register, label, addend))
        self.emit(isa.constant(register, 0))
        if self.target.pic:
            self.emit(isa.alu("add", register, ABI.pic_base_register, register))

    def branch(self, op, label):
        if not self.target.pic:
            old_size = 16 if self.target.fixed_instruction_width else 15
            self.control_fixups.append((len(self.code), "branch", op, label, old_size))
            self.emit(
                isa.constant(ABI.scratch_register, 0)
                + isa.jump("jmp", ABI.scratch_register)
            )
            return
        self.address(ABI.scratch_register, label)
        self.emit(isa.jump(op, ABI.scratch_register))

    def call(self, label):
        if not self.target.pic:
            old_size = 32 if self.target.fixed_instruction_width else 28
            self.control_fixups.append((len(self.code), "call", None, label, old_size))
            self.emit(
                isa.constant(ABI.scratch_register, 0)
                + self._call_bytes(ABI.scratch_register)
            )
            return
        self.address(ABI.scratch_register, label)
        self.call_register(ABI.scratch_register)

    def relax_controls(self):
        """Shrink symbolic fixed-address branches/calls after final layout."""
        if not self.control_fixups:
            return
        choices = {
            offset: old_size
            for offset, _, _, _, old_size in self.control_fixups
        }

        def translated(position):
            return position - sum(
                old_size - choices[offset]
                for offset, _, _, _, old_size in self.control_fixups
                if offset < position
            )

        while True:
            changed = False
            for offset, kind, _, label, old_size in self.control_fixups:
                if label not in self.labels:
                    raise CompileError(f"undefined symbol: {label}")
                target = self.target.load_address + translated(self.labels[label])
                size = (
                    4 if kind == "branch" else (20 if self.target.fixed_instruction_width else 17)
                ) if target <= 0xFFFF else old_size
                if choices[offset] != size:
                    choices[offset] = size
                    changed = True
            if not changed:
                break

        original = bytes(self.code)
        rebuilt = bytearray()
        cursor = 0
        for offset, kind, op, label, old_size in sorted(self.control_fixups):
            rebuilt.extend(original[cursor:offset])
            target = self.target.load_address + translated(self.labels[label])
            if kind == "branch":
                selected = (isa.jump(op, target, True) if target <= 0xFFFF
                            else isa.constant(ABI.scratch_register, target)
                            + isa.jump(op, ABI.scratch_register))
                rebuilt.extend(self._padded(selected))
            else:
                selected = (self._call_bytes(target, True) if target <= 0xFFFF
                            else isa.constant(ABI.scratch_register, target)
                            + self._call_bytes(ABI.scratch_register))
                rebuilt.extend(self._padded(selected))
            cursor = offset + old_size
        rebuilt.extend(original[cursor:])
        self.code = rebuilt
        self.labels = {
            label: translated(offset) for label, offset in self.labels.items()
        }
        self.fixups = [
            (translated(offset), register, label, addend)
            for offset, register, label, addend in self.fixups
        ]
        self.control_fixups.clear()

    def _padded(self, data):
        if not self.target.fixed_instruction_width:
            return data
        saved = self.code
        self.code = bytearray()
        self.emit(data)
        result = bytes(self.code)
        self.code = saved
        return result

    def finish(self):
        self.relax_controls()
        for offset, register, label, addend in self.fixups:
            if label not in self.labels:
                raise CompileError(f"undefined symbol: {label}")
            value = (
                self.labels[label]
                + addend
                + (0 if self.target.pic else self.target.load_address)
            )
            self.code[offset : offset + 12] = isa.constant(register, value)


__all__ = ["Assembler"]
