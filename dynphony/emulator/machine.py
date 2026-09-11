"""Small independent byte decoder for the instruction subset emitted by dyncc.

Comparison flags use an internal model; their hardware bit layout is not specified
by the supplied ISA. Generated code only uses named conditional branches, never
reads flag bits. This is a reference test runner, not a cycle-accurate CPU model.
"""

from collections import deque

from ..targets.dynphony.config import Target

MASK = 0xFFFFFFFF


def signed(value):
    return value - 2**32 if value & 0x80000000 else value


class Machine:
    def __init__(
        self,
        binary,
        ram_size=16 * 1024 * 1024,
        load_address=0,
        *,
        inputs=(),
        keyboard_inputs=(),
        time_value=0,
        persistent_size=0,
    ):
        Target(
            ram_size=ram_size,
            load_address=load_address,
            persistent_size=persistent_size,
        ).validate()
        if len(binary) > ram_size or load_address % ram_size + len(binary) > ram_size:
            raise ValueError("image does not fit contiguously in RAM")
        self.memory = bytearray(ram_size)
        self.mask = ram_size - 1
        self.regs = [0] * 16
        self.pc = load_address
        self.steps = 0
        self.comparison = None
        self.inputs = deque(value & MASK for value in inputs)
        self.keyboard_inputs = deque(value & MASK for value in keyboard_inputs)
        self.outputs = []
        self.screen_updates = []
        self.time_value = time_value & 0xFFFFFFFFFFFFFFFF
        self.persistent = bytearray(persistent_size)
        self.persistent_mask = persistent_size - 1 if persistent_size else None
        for i, b in enumerate(binary):
            self.memory[(load_address + i) & self.mask] = b

    def read(self, address, size):
        return int.from_bytes(
            bytes(self.memory[(address + i) & self.mask] for i in range(size)), "big"
        )

    def write(self, address, value, size):
        for i in range(size):
            self.memory[(address + i) & self.mask] = (
                value >> (8 * (size - i - 1))
            ) & 255

    def persistent_read(self, address):
        if self.persistent_mask is None:
            raise RuntimeError("persistent memory is not configured")
        return int.from_bytes(
            bytes(self.persistent[(address + i) & self.persistent_mask] for i in range(4)),
            "big",
        )

    def persistent_write(self, address, value):
        if self.persistent_mask is None:
            raise RuntimeError("persistent memory is not configured")
        for i in range(4):
            self.persistent[(address + i) & self.persistent_mask] = (
                value >> (8 * (3 - i))
            ) & 255

    def step(self):
        pc = self.pc
        op = self.read(pc, 1)
        r = self.regs
        next_pc = pc + 1
        if op == 0:
            pass
        elif op == 1:
            destination = self.read(pc + 1, 1) >> 4
            r[destination] = self.inputs.popleft() if self.inputs else 0
            next_pc = pc + 2
        elif op == 2:
            self.outputs.append(r[self.read(pc + 2, 1) & 15])
            next_pc = pc + 3
        elif op == 0x12:
            self.outputs.append(self.read(pc + 2, 2))
            next_pc = pc + 4
        elif op == 3:
            destination = self.read(pc + 1, 1) >> 4
            r[destination] = (
                self.keyboard_inputs.popleft() if self.keyboard_inputs else 0
            )
            next_pc = pc + 2
        elif op in (4, 0x14):
            setting = r[self.read(pc + 1, 1) & 15]
            immediate = op == 0x14
            value = (
                self.read(pc + 2, 2)
                if immediate
                else r[self.read(pc + 2, 1) & 15]
            )
            self.screen_updates.append((setting, value))
            next_pc = pc + (4 if immediate else 3)
        elif op in (5, 6):
            destination = self.read(pc + 1, 1) >> 4
            r[destination] = (
                self.time_value if op == 5 else self.time_value >> 32
            ) & MASK
            next_pc = pc + 2
        elif op == 7:
            destination = self.read(pc + 1, 1) >> 4
            r[destination] = pc
            if destination == 15:
                self.comparison = None
            next_pc = pc + 2
        elif 0x20 <= op <= 0x3A and (op & 15) <= 10:
            pair = self.read(pc + 1, 1)
            dst, left = pair >> 4, pair & 15
            immediate = bool(op & 16)
            right = self.read(pc + 2, 2) if immediate else r[self.read(pc + 2, 1) & 15]
            a = r[left]
            code = op & 15
            next_pc = pc + (4 if immediate else 3)
            if code == 10:
                self.comparison = (a, right)
                # A token, rather than an invented hardware flags bit layout.
                r[dst] = 0
            else:
                operations = {
                    0: lambda: ~(a & right),
                    1: lambda: a | right,
                    2: lambda: a & right,
                    3: lambda: ~(a | right),
                    4: lambda: a + right,
                    5: lambda: a - right,
                    6: lambda: a ^ right,
                    7: lambda: (a << right) if right < 32 else 0,
                    8: lambda: (a >> right) if right < 32 else 0,
                    9: lambda: (signed(a) >> min(right, 32)),
                }
                r[dst] = operations[code]() & MASK
                if dst == 15:
                    self.comparison = None
        elif 0x40 <= op <= 0x5F:
            immediate = bool(op & 16)
            base = op & 0xEF
            target = self.read(pc + 2, 2) if immediate else r[self.read(pc + 2, 1) & 15]
            next_pc = pc + (4 if immediate else 3)
            if base == 0x48:
                take = True
            else:
                if self.comparison is None:
                    raise RuntimeError("conditional branch without comparison")
                a, b = self.comparison
                conditions = {
                    0x41: a == b,
                    0x49: a != b,
                    0x42: a < b,
                    0x4A: a >= b,
                    0x43: a <= b,
                    0x4B: a > b,
                    0x44: signed(a) < signed(b),
                    0x4C: signed(a) >= signed(b),
                    0x45: signed(a) <= signed(b),
                    0x4D: signed(a) > signed(b),
                }
                if base not in conditions:
                    raise RuntimeError(f"unknown branch {op:#x}")
                take = conditions[base]
            if take:
                next_pc = target
        elif 0x60 <= op <= 0x77:
            immediate = bool(op & 16)
            code = op & 7
            operand = self.read(pc + 1, 1)
            address = (
                self.read(pc + 2, 2) if immediate else r[self.read(pc + 2, 1) & 15]
            )
            next_pc = pc + (4 if immediate else 3)
            if code == 3:
                r[operand >> 4] = self.persistent_read(address)
            elif code == 7:
                self.persistent_write(address, r[operand & 15])
            elif code < 4:
                size = (1, 2, 4)[code]
                r[operand >> 4] = self.read(address, size)
                if operand >> 4 == 15:
                    self.comparison = None
            else:
                size = (1, 2, 4)[code & 3]
                self.write(address, r[operand & 15], size)
        else:
            raise RuntimeError(f"unsupported opcode {op:#x} at {pc:#x}")
        r[0] = 0
        self.pc = next_pc & MASK
        self.steps += 1

    def run(
        self,
        halt_address=None,
        max_steps=5_000_000,
        progress=None,
        progress_interval=250_000,
    ):
        next_progress = self.steps + progress_interval
        while self.steps < max_steps:
            if halt_address is not None and self.pc == halt_address:
                return self.regs[1]
            previous = self.pc
            self.step()
            if progress is not None and self.steps >= next_progress:
                progress(self)
                next_progress = self.steps + progress_interval
            if self.pc == previous:
                return self.regs[1]
        raise RuntimeError(
            f"execution limit exceeded ({max_steps} instructions), PC={self.pc:#x}"
        )
