"""Target types and typed syntax. No parser or machine encoding dependencies."""

from dataclasses import dataclass, field
from typing import Any


class CompileError(Exception):
    """A source or target configuration diagnostic safe to show to a user."""


@dataclass(frozen=True)
class Type:
    kind: str = "int"
    size: int = 4
    signed: bool = True
    base: "Type | None" = None
    count: int = 0
    params: tuple = ()

    @property
    def align(self):
        return self.base.align if self.kind == "array" else min(max(self.size, 1), 4)

    @property
    def integer(self):
        return self.kind == "int"

    def decay(self):
        return (
            pointer(self.base)
            if self.kind == "array"
            else pointer(self) if self.kind == "function" else self
        )

    def promote(self):
        return INT if self.integer and self.size < 4 else self.decay()

    def __str__(self):
        if self.kind == "pointer":
            return f"{self.base}*"
        if self.kind == "array":
            return f"{self.base}[{self.count}]"
        if self.kind == "function":
            return f'{self.base}({", ".join(map(str, self.params))})'
        if self.kind == "void":
            return "void"
        return f'{"i" if self.signed else "u"}{self.size * 8}'


INT = Type()
UINT = Type(signed=False)
CHAR = Type(size=1, signed=False)
VOID = Type("void", 0, False)


def pointer(base):
    return Type("pointer", 4, False, base)


def array(base, count):
    return Type("array", base.size * count, False, base, count)


def common(a, b):
    a, b = a.promote(), b.promote()
    if not a.integer or not b.integer:
        raise CompileError("integer operands required")
    return UINT if not a.signed or not b.signed else INT


@dataclass
class Symbol:
    name: str
    type: Type
    storage: str  # global, function, local, parameter
    key: str


@dataclass
class Node:
    op: str
    type: Type = VOID
    children: list["Node"] = field(default_factory=list)
    value: Any = None
    location: str = ""
    lvalue: bool = False


@dataclass
class Global:
    symbol: Symbol
    data: bytearray
    relocations: list[tuple[int, str, int]] = field(default_factory=list)


@dataclass
class Function:
    symbol: Symbol
    params: list[Symbol]
    locals: list[Symbol]
    body: Node


@dataclass
class Program:
    globals: list[Global]
    functions: list[Function]


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment
