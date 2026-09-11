"""Explicit control flow, virtual values, memory operations and function calls.

Virtual values are immutable except for 'copy' at control-flow joins. Addresses
are ordinary values. The backend never sees parser nodes or C expression trees.
"""

from dataclasses import dataclass, field
from .model import CHAR, INT, UINT, VOID, Type, pointer, common


@dataclass
class Instruction:
    op: str
    dst: int | None = None
    args: tuple = ()
    type: Type = VOID
    extra: object = None


@dataclass
class FunctionIR:
    name: str
    params: list
    locals: list
    instructions: list[Instruction] = field(default_factory=list)
    values: int = 0


@dataclass
class ModuleIR:
    globals: list
    functions: list[FunctionIR]

    def dump(self):
        lines = []
        for f in self.functions:
            lines.append(f'function {f.name}({", ".join(p.key for p in f.params)}):')
            for i in f.instructions:
                dst = f"%{i.dst} = " if i.dst is not None else ""
                lines.append(
                    f'  {dst}{i.op} {i.args} {i.extra if i.extra is not None else ""} : {i.type}'
                )
        return "\n".join(lines) + "\n"


class Lowerer:
    def __init__(self, function):
        self.f = FunctionIR(function.symbol.key, function.params, function.locals)
        self.label_id = 0
        self.loops = []
        self.scopes = []
        self.dynamic_locals = {}

    def value(self):
        v = self.f.values
        self.f.values += 1
        return v

    def emit(self, op, args=(), type_=VOID, extra=None, result=True):
        dst = self.value() if result else None
        self.f.instructions.append(Instruction(op, dst, tuple(args), type_, extra))
        return dst

    def label(self):
        self.label_id += 1
        return f"{self.f.name}.L{self.label_id}"

    def mark(self, label):
        self.emit("label", extra=label, result=False)

    def jump(self, label):
        self.emit("jump", extra=label, result=False)

    def branch(self, value, target, truthy=True):
        """Branch to target on the requested truth value; otherwise fall through."""
        self.emit(
            "branch_if", (value,), extra=(truthy, target), result=False
        )

    def const(self, value, t=INT):
        return self.emit("const", type_=t, extra=value)

    def cast(self, v, t):
        return self.emit("cast", (v,), t)

    def store(self, address, value, t):
        self.emit("store", (address, value), t, result=False)

    def address(self, n):
        if n.op == "var":
            sym = n.value
            if sym.key in self.dynamic_locals:
                return self.dynamic_locals[sym.key]
            return self.emit(
                (
                    "global_addr"
                    if sym.storage in ("global", "function")
                    else "local_addr"
                ),
                type_=pointer(sym.type),
                extra=sym.key,
            )
        if n.op == "deref":
            return self.expr(n.children[0])
        if n.op == "member":
            base = self.expr(n.children[0])
            return (
                self.binary("+", base, self.const(n.value), UINT)
                if n.value
                else base
            )
        raise AssertionError(f"non-addressable typed node: {n.op}")

    def binary(self, op, a, b, t):
        return self.emit("binary", (a, b), t, op)

    def scaled(self, a, b, scale):
        if scale != 1:
            b = self.binary("*", b, self.const(scale), INT)
        return self.binary("+", a, b, UINT)

    def expr(self, n):
        op = n.op
        if op == "const":
            return self.const(n.value, n.type)
        if op in ("var", "deref", "member"):
            return self.emit("load", (self.address(n),), n.type)
        if op == "address":
            return self.address(n.children[0])
        if op == "cast":
            return self.cast(self.expr(n.children[0]), n.type)
        if op == "bool_cast":
            return self.binary("!=", self.expr(n.children[0]), self.const(0), n.type)
        if op == "unary":
            return self.emit("unary", (self.expr(n.children[0]),), n.type, n.value)
        if op == "pointer_add":
            return self.scaled(
                self.expr(n.children[0]), self.expr(n.children[1]), n.value
            )
        if op == "pointer_diff":
            d = self.binary(
                "-", self.expr(n.children[0]), self.expr(n.children[1]), INT
            )
            return self.binary("/", d, self.const(n.value), INT)
        if op == "binary":
            if n.value in ("&&", "||"):
                result = self.value()
                rhs, short, end = self.label(), self.label(), self.label()
                a = self.expr(n.children[0])
                # The short-circuit block is laid out next. Jump only when the
                # right operand must be evaluated.
                self.branch(a, rhs, n.value == "&&")
                self.mark(short)
                v = self.const(int(n.value == "||"))
                self.f.instructions.append(Instruction("copy", result, (v,), INT))
                self.jump(end)
                self.mark(rhs)
                b = self.expr(n.children[1])
                v = self.binary("!=", b, self.const(0), UINT)
                self.f.instructions.append(Instruction("copy", result, (v,), INT))
                self.mark(end)
                return result
            return self.binary(
                n.value,
                self.expr(n.children[0]),
                self.expr(n.children[1]),
                n.children[0].type,
            )
        if op == "assign":
            addr = self.address(n.children[0])
            value = self.expr(n.children[1])
            self.store(addr, value, n.type)
            return value
        if op == "compound_assign":
            lhs, rhs = n.children
            addr = self.address(lhs)
            a = self.emit("load", (addr,), lhs.type)
            b = self.expr(rhs)
            if lhs.type.kind == "pointer":
                value = self.scaled(
                    a, b, lhs.type.base.size * (1 if n.value == "+" else -1)
                )
            else:
                t = (
                    lhs.type.promote()
                    if n.value in ("<<", ">>")
                    else common(lhs.type, rhs.type)
                )
                value = self.binary(
                    n.value,
                    self.cast(a, t),
                    self.cast(b, rhs.type.promote() if n.value in ("<<", ">>") else t),
                    t,
                )
            value = self.cast(value, lhs.type)
            self.store(addr, value, lhs.type)
            return value
        if op == "increment":
            lhs = n.children[0]
            addr = self.address(lhs)
            old = self.emit("load", (addr,), lhs.type)
            step = lhs.type.base.size if lhs.type.kind == "pointer" else 1
            value = self.binary(
                "+" if "+" in n.value else "-",
                old,
                self.const(step),
                lhs.type.promote(),
            )
            value = self.cast(value, lhs.type)
            self.store(addr, value, lhs.type)
            return old if n.value.startswith("p") else value
        if op == "call":
            target = n.children[0]
            arguments = [self.expr(x) for x in n.children[1:]]
            if (
                target.op == "address"
                and target.children[0].op == "var"
                and target.children[0].value.storage == "function"
            ):
                return self.emit(
                    "direct_call",
                    arguments,
                    n.type,
                    target.children[0].value.key,
                )
            return self.emit("call", [self.expr(target), *arguments], n.type)
        if op == "printf":
            format_ = self.expr(n.children[0])
            arguments = iter(n.children[1:])
            result = self.const(0, INT)
            for token in n.value:
                if token[0] == "literal":
                    _, offset, count = token
                    text = (
                        self.binary("+", format_, self.const(offset, UINT), UINT)
                        if offset
                        else format_
                    )
                    written = self.emit(
                        "direct_call",
                        (text, self.const(count, UINT)),
                        UINT,
                        "__dyn_printf_write",
                    )
                else:
                    helper = {
                        "d": "__dyn_printf_signed",
                        "u": "__dyn_printf_unsigned",
                        "x": "__dyn_printf_hex",
                        "c": "__dyn_printf_put",
                        "s": "__dyn_printf_string",
                    }[token[0]]
                    written = self.emit(
                        "direct_call", (self.expr(next(arguments)),), UINT, helper
                    )
                result = self.binary("+", result, written, INT)
            return result
        if op == "comma":
            for child in n.children:
                result = self.expr(child)
            return result
        if op == "select":
            result = self.value()
            yes, no, end = self.label(), self.label(), self.label()
            self.branch(self.expr(n.children[0]), no, False)
            self.mark(yes)
            a = self.expr(n.children[1])
            self.f.instructions.append(Instruction("copy", result, (a,), n.type))
            self.jump(end)
            self.mark(no)
            b = self.expr(n.children[2])
            self.f.instructions.append(Instruction("copy", result, (b,), n.type))
            self.mark(end)
            return result
        raise AssertionError(f"unhandled typed expression {op}")

    def statement(self, n):
        op = n.op
        if op == "block":
            dynamic = any(
                child.op == "declare" and child.value[0].type.kind == "vla"
                for child in n.children
            )
            marker = self.emit("stack_mark", type_=UINT) if dynamic else None
            self.scopes.append(marker)
            for child in n.children:
                self.statement(child)
            self.scopes.pop()
            if marker is not None:
                self.emit("stack_restore", (marker,), result=False)
        elif op == "expression":
            self.expr(n.children[0])
        elif op == "declare":
            sym, entries, bound = n.value
            if sym.type.kind == "vla":
                bytes_ = self.expr(bound)
                if sym.type.base.size != 1:
                    bytes_ = self.binary(
                        "*", bytes_, self.const(sym.type.base.size, UINT), UINT
                    )
                align = sym.type.base.align
                if align > 1:
                    bytes_ = self.binary(
                        "+", bytes_, self.const(align - 1, UINT), UINT
                    )
                    bytes_ = self.binary(
                        "&", bytes_, self.const(-(align), UINT), UINT
                    )
                self.dynamic_locals[sym.key] = self.emit(
                    "stack_alloc", (bytes_,), pointer(sym.type.base)
                )
                return
            if entries is not None:
                addr = self.emit("local_addr", type_=pointer(sym.type), extra=sym.key)
                if sym.type.kind in ("array", "struct"):
                    self.emit("zero", (addr,), extra=sym.type.size, result=False)
                for off, t, value in entries:
                    p = self.binary("+", addr, self.const(off), UINT) if off else addr
                    self.store(p, self.expr(value), t)
        elif op == "return":
            for marker in reversed(self.scopes):
                if marker is not None:
                    self.emit("stack_restore", (marker,), result=False)
            self.emit(
                "return",
                (self.expr(n.children[0]),) if n.children else (),
                result=False,
            )
        elif op == "if":
            yes, no, end = self.label(), self.label(), self.label()
            self.branch(self.expr(n.children[0]), no, False)
            self.mark(yes)
            self.statement(n.children[1])
            self.jump(end)
            self.mark(no)
            self.statement(n.children[2])
            self.mark(end)
        elif op in ("while", "for", "do"):
            test, body, step, end = (
                self.label(),
                self.label(),
                self.label(),
                self.label(),
            )
            loop_marker = None
            if op == "for" and n.children[0].op == "declare" and n.children[0].value[0].type.kind == "vla":
                loop_marker = self.emit("stack_mark", type_=UINT)
                self.scopes.append(loop_marker)
            if op == "for":
                self.statement(n.children[0])
                cond = n.children[1]
                stmt = n.children[3]
            else:
                cond, stmt = n.children
            self.loops.append((end, step, len(self.scopes) - (1 if loop_marker is not None else 0)))
            if op == "do":
                self.jump(body)
            self.mark(test)
            self.branch(self.expr(cond), end, False)
            self.mark(body)
            self.statement(stmt)
            self.mark(step)
            if op == "for":
                self.statement(n.children[2])
            self.jump(test)
            self.mark(end)
            self.loops.pop()
            if loop_marker is not None:
                self.scopes.pop()
                self.emit("stack_restore", (loop_marker,), result=False)
        elif op == "break":
            end, _, scope_start = self.loops[-1]
            for marker in reversed(self.scopes[scope_start:]):
                if marker is not None:
                    self.emit("stack_restore", (marker,), result=False)
            self.jump(end)
        elif op == "continue":
            _, step, scope_start = self.loops[-1]
            for marker in reversed(self.scopes[scope_start + 1 :]):
                if marker is not None:
                    self.emit("stack_restore", (marker,), result=False)
            self.jump(step)
        else:
            raise AssertionError(f"unhandled typed statement {op}")


def lower(program):
    functions = []
    for f in program.functions:
        l = Lowerer(f)
        l.statement(f.body)
        # Deterministic fallthrough; only main's implicit return is specified by C.
        l.emit(
            "return",
            () if f.symbol.type.base == VOID else (l.const(0),),
            result=False,
        )
        functions.append(l.f)

    main = next(function for function in program.functions if function.symbol.key == "main")
    startup = FunctionIR("_start", [], [])
    startup.instructions.extend(
        [
            Instruction("init_pic"),
            Instruction("init_stack"),
            Instruction("relocate_globals"),
        ]
    )
    if any(g.symbol.key == "__dyn_printf_framebuffer" for g in program.globals):
        address = startup.values
        startup.values += 1
        startup.instructions.append(
            Instruction("global_addr", address, (), pointer(CHAR), "__dyn_printf_framebuffer")
        )
        startup.instructions.append(
            Instruction("init_text_screen", args=(address,))
        )
    result = startup.values
    startup.values += 1
    startup.instructions.append(
        Instruction("direct_call", result, (), main.symbol.type.base, "main")
    )
    startup.instructions.append(Instruction("halt", args=(result,)))
    return ModuleIR(program.globals, [startup, *functions])
