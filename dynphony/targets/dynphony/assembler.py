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
        self.code.extend(data)

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
            self.control_fixups.append((len(self.code), "branch", op, label, 15))
            self.emit(bytes(15))
            return
        self.address(ABI.scratch_register, label)
        self.emit(isa.jump(op, ABI.scratch_register))

    def call(self, label):
        if not self.target.pic:
            self.control_fixups.append((len(self.code), "call", None, label, 28))
            self.emit(bytes(28))
            return
        self.address(ABI.scratch_register, label)
        self.emit(isa.call(ABI.scratch_register))

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
                    4 if kind == "branch" else 17
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
                rebuilt.extend(
                    isa.jump(op, target, True)
                    if target <= 0xFFFF
                    else isa.constant(ABI.scratch_register, target)
                    + isa.jump(op, ABI.scratch_register)
                )
            else:
                rebuilt.extend(
                    isa.call(target, True)
                    if target <= 0xFFFF
                    else isa.constant(ABI.scratch_register, target)
                    + isa.call(ABI.scratch_register)
                )
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
