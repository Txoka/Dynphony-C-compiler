"""pycparser lexer/parser adapter and semantic conversion to our typed AST."""

import re
from pycparser import c_ast as c, c_parser
from .model import (
    CompileError,
    Type,
    INT,
    UINT,
    CHAR,
    VOID,
    pointer,
    array,
    common,
    Symbol,
    Node,
    Global,
    Function,
    Program,
)


def strip_comments(source):
    # Preserve strings, character literals, source lines, and token separation.
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*'
    return re.sub(
        pattern,
        lambda m: (
            re.sub(r"[^\n]", " ", m[0]) if m[0].startswith(("/*", "//")) else m[0]
        ),
        source,
    )


def parse(source, filename="<input>"):
    source = strip_comments(source)
    if re.search(r"^\s*#", source, re.M):
        raise CompileError(f"{filename}: preprocessor directives are unsupported")
    try:
        return c_parser.CParser().parse(source, filename)
    except Exception as exc:
        raise CompileError(str(exc)) from exc


def literal(text):
    s = re.sub("[uUlL]+$", "", text)
    if s.startswith(("0x", "0X")):
        return int(s, 16)
    if len(s) > 1 and s[0] == "0":
        return int(s, 8)
    return int(s, 10)


def literal_bytes(text, quote='"'):
    """Decode C escapes explicitly (Python's escape rules are not identical)."""
    data = bytearray()
    escapes = {
        "a": 7,
        "b": 8,
        "f": 12,
        "n": 10,
        "r": 13,
        "t": 9,
        "v": 11,
        chr(92): 92,
        "'": 39,
        '"': 34,
        "?": 63,
    }
    i = 0
    tokens = 0
    while i < len(text):
        if text[i].isspace():
            i += 1
            continue
        if text[i] != quote:
            raise CompileError("only ordinary single-byte literals are supported")
        tokens += 1
        i += 1
        while i < len(text) and text[i] != quote:
            ch = text[i]
            i += 1
            if ord(ch) != 92:
                value = ord(ch)
            else:
                if i == len(text):
                    raise CompileError("unterminated escape sequence")
                ch = text[i]
                i += 1
                if ch in escapes:
                    value = escapes[ch]
                elif ch in "01234567":
                    digits = ch
                    while i < len(text) and len(digits) < 3 and text[i] in "01234567":
                        digits += text[i]
                        i += 1
                    value = int(digits, 8)
                elif ch == "x":
                    start = i
                    while i < len(text) and text[i] in "0123456789abcdefABCDEF":
                        i += 1
                    if i == start:
                        raise CompileError("hexadecimal escape requires a digit")
                    value = int(text[start:i], 16)
                else:
                    raise CompileError(f"unsupported escape sequence: {chr(92)}{ch}")
            if value > 255:
                raise CompileError("literal character exceeds one byte")
            data.append(value)
        if i == len(text):
            raise CompileError("unterminated literal")
        i += 1
    if not tokens or (quote == "'" and tokens != 1):
        raise CompileError("invalid literal")
    return bytes(data)


def string_bytes(text):
    return literal_bytes(text) + bytes([0])


class Frontend:
    def __init__(self):
        self.scopes = [{}]
        self.typedefs = {}
        self.globals = []
        self.functions = []
        self.locals = []
        self.serial = 0
        self.loop_depth = 0
        self.return_type = VOID

    def fail(self, source, message):
        raise CompileError(f'{getattr(source, "coord", "")}: {message}')

    def new(self, name, type_, storage):
        self.serial += 1
        return Symbol(
            name,
            type_,
            storage,
            name if storage in ("global", "function") else f"{name}.{self.serial}",
        )

    def lookup(self, source):
        for scope in reversed(self.scopes):
            if source.name in scope:
                return scope[source.name]
        self.fail(source, f"undeclared identifier {source.name}")

    def typename(self, t):
        if getattr(t, "quals", None):
            self.fail(
                t, "qualified types (const/volatile/restrict) are not yet supported"
            )
        if isinstance(t, (c.Decl, c.Typename, c.Typedef)):
            return self.typename(t.type)
        if isinstance(t, c.TypeDecl):
            return self.typename(t.type)
        if isinstance(t, c.IdentifierType):
            names = t.names
            if len(names) == 1 and names[0] in self.typedefs:
                return self.typedefs[names[0]]
            if names == ["void"]:
                return VOID
            if names == ["_Bool"]:
                self.fail(t, "_Bool is not yet supported")
            if any(
                n not in ("signed", "unsigned", "char", "short", "int", "long")
                for n in names
            ):
                self.fail(t, "only integer, pointer and array types are supported")
            if names.count("long") > 1:
                self.fail(t, "64-bit long long is not yet supported")
            if (
                len(set(names)) != len(names)
                or ("signed" in names and "unsigned" in names)
                or (
                    "char" in names
                    and any(x in names for x in ("short", "int", "long"))
                )
                or ("short" in names and "long" in names)
            ):
                self.fail(t, "invalid integer type specifiers")
            size = 1 if "char" in names else 2 if "short" in names else 4
            signed = "unsigned" not in names and (
                "char" not in names or "signed" in names
            )
            return Type(size=size, signed=signed)
        if isinstance(t, c.PtrDecl):
            return pointer(self.typename(t.type))
        if isinstance(t, c.ArrayDecl):
            base = self.typename(t.type)
            count = self.const_int(t.dim) if t.dim else 0
            if count < 0 or base.size == 0:
                self.fail(t, "invalid array type")
            return array(base, count)
        if isinstance(t, c.FuncDecl):
            params = []
            if t.args:
                for p in t.args.params:
                    if isinstance(p, c.EllipsisParam):
                        self.fail(p, "variadic functions are unsupported")
                    pt = self.typename(p).decay()
                    params.append(pt)
                if params == [VOID]:
                    params = []
            if len(params) > 6:
                self.fail(t, "at most six function arguments are supported")
            result = self.typename(t.type)
            if result.kind in ("array", "function"):
                self.fail(t, "invalid function return type")
            return Type("function", 0, False, result, params=tuple(params))
        self.fail(t, f"unsupported type: {type(t).__name__}")

    def const_int(self, source):
        try:
            value = self.static_value(self.expr(source))
        except (ZeroDivisionError, ValueError):
            self.fail(source, "invalid integer constant expression")
        if not isinstance(value, int):
            self.fail(source, "integer constant expression required")
        return value

    def node(self, source, op, type_=VOID, children=None, value=None, lvalue=False):
        return Node(
            op, type_, children or [], value, str(getattr(source, "coord", "")), lvalue
        )

    def cast(self, n, t):
        if n.type == t:
            return n
        return Node("cast", t, [n], location=n.location)

    def value(self, n):
        if n.type.kind in ("array", "function"):
            return Node("address", n.type.decay(), [n], location=n.location)
        return n

    def scalar(self, source, n):
        n = self.value(n)
        if n.type.kind not in ("int", "pointer"):
            self.fail(source, "scalar expression required")
        return n

    def convert(self, source, n, t):
        n = self.value(n)
        if n.type.integer and t.integer:
            return self.cast(n, t)
        if t.kind == "pointer":
            if n.type.kind == "pointer" and (
                n.type == t or n.type.base == VOID or t.base == VOID
            ):
                return self.cast(n, t)
            if n.op == "const" and n.value == 0:
                return self.cast(n, t)
        self.fail(source, f"cannot convert {n.type} to {t}")

    def binary(self, source, op, left, right):
        a, b = self.value(left), self.value(right)
        if op in ("&&", "||"):
            return self.node(
                source,
                "binary",
                INT,
                [self.scalar(source, a), self.scalar(source, b)],
                op,
            )
        if op == "+" and b.type.kind == "pointer" and a.type.integer:
            a, b = b, a
        if op in ("+", "-") and a.type.kind == "pointer":
            if not a.type.base.size:
                self.fail(source, "arithmetic on incomplete/function pointer")
            if b.type.integer:
                return self.node(
                    source,
                    "pointer_add",
                    a.type,
                    [a, self.cast(b, INT)],
                    a.type.base.size * (1 if op == "+" else -1),
                )
            if op == "-" and b.type == a.type:
                return self.node(source, "pointer_diff", INT, [a, b], a.type.base.size)
        if op in ("==", "!=", "<", "<=", ">", ">=") and (
            a.type.kind == "pointer" or b.type.kind == "pointer"
        ):
            t = a.type if a.type.kind == "pointer" else b.type
            a, b = self.convert(source, a, t), self.convert(source, b, t)
        else:
            t = common(a.type, b.type)
            if op in ("<<", ">>"):
                t = a.type.promote()
                a, b = self.cast(a, t), self.cast(b, b.type.promote())
            else:
                a, b = self.cast(a, t), self.cast(b, t)
        result = INT if op in ("==", "!=", "<", "<=", ">", ">=") else t
        return self.node(source, "binary", result, [a, b], op)

    def expr(self, s):
        if isinstance(s, c.Constant):
            if s.type == "string":
                data = string_bytes(s.value)
                sym = self.new(
                    f"__string_{self.serial}", array(CHAR, len(data)), "global"
                )
                self.globals.append(Global(sym, bytearray(data)))
                return self.node(s, "var", sym.type, value=sym, lvalue=True)
            if s.type == "char":
                data = literal_bytes(s.value, "'")
                if len(data) != 1:
                    self.fail(s, "only single-character constants are supported")
                v = data[0]
                return self.node(s, "const", INT, value=v)
            if "float" in s.type or "double" in s.type:
                self.fail(s, "floating point is unsupported")
            if "ll" in s.value.lower():
                self.fail(s, "64-bit literals are unsupported")
            v = literal(s.value)
            if v > 0xFFFFFFFF:
                self.fail(s, "integer literal exceeds 32 bits")
            unsigned = "u" in s.value.lower()
            if v > 0x7FFFFFFF and not unsigned:
                if s.value.startswith("0"):
                    unsigned = True
                else:
                    self.fail(
                        s,
                        "decimal literal needs unsupported 64-bit type; use a U suffix for unsigned values",
                    )
            return self.node(s, "const", UINT if unsigned else INT, value=v)
        if isinstance(s, c.ID):
            sym = self.lookup(s)
            return self.node(
                s, "var", sym.type, value=sym, lvalue=sym.storage != "function"
            )
        if isinstance(s, c.BinaryOp):
            return self.binary(s, s.op, self.expr(s.left), self.expr(s.right))
        if isinstance(s, c.ArrayRef):
            p = self.binary(s, "+", self.expr(s.name), self.expr(s.subscript))
            if p.type.kind != "pointer":
                self.fail(s, "array subscript requires a pointer")
            return self.node(s, "deref", p.type.base, [p], lvalue=True)
        if isinstance(s, c.UnaryOp):
            if s.op == "sizeof":
                t = (
                    self.typename(s.expr)
                    if isinstance(s.expr, c.Typename)
                    else self.expr(s.expr).type
                )
                if not t.size:
                    self.fail(s, "sizeof requires a complete object type")
                return self.node(s, "const", UINT, value=t.size)
            a = self.expr(s.expr)
            if s.op == "&":
                if not a.lvalue and a.type.kind != "function":
                    self.fail(s, "address-of requires an lvalue")
                return self.node(s, "address", pointer(a.type), [a])
            if s.op == "*":
                a = self.value(a)
                if a.type.kind != "pointer" or a.type.base == VOID:
                    self.fail(s, "dereference requires a non-void pointer")
                return self.node(
                    s, "deref", a.type.base, [a], lvalue=a.type.base.kind != "function"
                )
            if s.op in ("++", "--", "p++", "p--"):
                self.modifiable(s, a)
                self.scalar(s, a)
                if a.type.kind == "pointer" and not a.type.base.size:
                    self.fail(s, "invalid pointer increment")
                return self.node(s, "increment", a.type, [a], s.op)
            a = self.scalar(s, a)
            if s.op == "!":
                return self.node(s, "unary", INT, [a], "!")
            if not a.type.integer:
                self.fail(s, "integer unary operand required")
            a = self.cast(a, a.type.promote())
            if s.op == "+":
                return a
            if s.op in ("-", "~"):
                return self.node(s, "unary", a.type, [a], s.op)
            self.fail(s, "unsupported unary operator")
        if isinstance(s, c.Cast):
            t, a = self.typename(s.to_type), self.value(self.expr(s.expr))
            if t == VOID:
                return self.cast(a, t)
            if t.kind not in ("int", "pointer") or a.type.kind not in (
                "int",
                "pointer",
            ):
                self.fail(s, "unsupported cast")
            return self.cast(a, t)
        if isinstance(s, c.Assignment):
            a, b = self.expr(s.lvalue), self.expr(s.rvalue)
            self.modifiable(s, a)
            if s.op == "=":
                b = self.convert(s, b, a.type)
            else:
                # Keep the lvalue as a single node; lower its address only once.
                probe = self.binary(s, s.op[:-1], a, b)
                self.convert(s, probe, a.type)
                return self.node(s, "compound_assign", a.type, [a, b], s.op[:-1])
            return self.node(s, "assign", a.type, [a, b])
        if isinstance(s, c.FuncCall):
            fn = self.value(self.expr(s.name))
            if fn.type.kind != "pointer" or fn.type.base.kind != "function":
                self.fail(s, "call requires a function")
            ft = fn.type.base
            args = s.args.exprs if s.args else []
            if len(args) != len(ft.params):
                self.fail(s, f"expected {len(ft.params)} arguments, got {len(args)}")
            values = [self.convert(s, self.expr(a), t) for a, t in zip(args, ft.params)]
            return self.node(s, "call", ft.base, [fn] + values)
        if isinstance(s, c.ExprList):
            values = [self.value(self.expr(a)) for a in s.exprs]
            return self.node(s, "comma", values[-1].type, values)
        if isinstance(s, c.TernaryOp):
            cond = self.scalar(s, self.expr(s.cond))
            a, b = self.value(self.expr(s.iftrue)), self.value(self.expr(s.iffalse))
            t = (
                a.type
                if a.type.kind == "pointer"
                else b.type if b.type.kind == "pointer" else common(a.type, b.type)
            )
            return self.node(
                s, "select", t, [cond, self.convert(s, a, t), self.convert(s, b, t)]
            )
        self.fail(s, f"unsupported expression: {type(s).__name__}")

    def modifiable(self, s, n):
        if not n.lvalue or n.type.kind in ("array", "function", "void"):
            self.fail(s, "modifiable lvalue required")

    def resolve_array(self, s, t):
        if t.kind == "array" and not t.count:
            if isinstance(s.init, c.InitList):
                t = array(t.base, len(s.init.exprs))
            elif isinstance(s.init, c.Constant) and s.init.type == "string":
                t = array(t.base, len(string_bytes(s.init.value)))
            else:
                self.fail(s, "incomplete arrays need an initializer")
        return t

    def initializer(self, s, t, init):
        """Flatten aggregate initializers to typed scalar entries at byte offsets."""
        if t.kind == "array":
            if (
                isinstance(init, c.Constant)
                and init.type == "string"
                and t.base.size == 1
            ):
                data = string_bytes(init.value)
                if len(data) - 1 > t.count:
                    self.fail(s, "string initializer too long")
                return [
                    (i, t.base, self.node(init, "const", INT, value=b))
                    for i, b in enumerate(data[: t.count])
                ]
            if not isinstance(init, c.InitList):
                self.fail(s, "array requires a brace or string initializer")
            if len(init.exprs) > t.count:
                self.fail(s, "too many array initializers")
            out = []
            for i, e in enumerate(init.exprs):
                out.extend(
                    (i * t.base.size + off, typ, n)
                    for off, typ, n in self.initializer(s, t.base, e)
                )
            return out
        if isinstance(init, c.InitList):
            if len(init.exprs) != 1:
                self.fail(s, "scalar initializer requires one value")
            init = init.exprs[0]
        return [(0, t, self.convert(s, self.expr(init), t))]

    def statement(self, s):
        if s is None:
            return Node("block")
        if isinstance(s, c.Compound):
            self.scopes.append({})
            body = [self.statement(x) for x in s.block_items or []]
            self.scopes.pop()
            return self.node(s, "block", children=body)
        if isinstance(s, c.DeclList):
            return self.node(s, "block", children=[self.statement(x) for x in s.decls])
        if isinstance(s, c.Decl):
            if s.storage:
                self.fail(
                    s,
                    "local storage specifiers are unsupported (use file-scope globals)",
                )
            t = self.resolve_array(s, self.typename(s))
            if t.kind in ("void", "function") or not t.size:
                self.fail(s, "local variable requires a complete object type")
            if s.name in self.scopes[-1]:
                self.fail(s, "duplicate local declaration")
            sym = self.new(s.name, t, "local")
            self.scopes[-1][s.name] = sym
            self.locals.append(sym)
            entries = self.initializer(s, t, s.init) if s.init else None
            return self.node(s, "declare", value=(sym, entries))
        if isinstance(s, c.Return):
            if self.return_type == VOID:
                if s.expr:
                    self.fail(s, "void function cannot return a value")
                children = []
            else:
                if not s.expr:
                    self.fail(s, "non-void function must return a value")
                children = [self.convert(s, self.expr(s.expr), self.return_type)]
            return self.node(s, "return", children=children)
        if isinstance(s, c.If):
            return self.node(
                s,
                "if",
                children=[
                    self.scalar(s, self.expr(s.cond)),
                    self.statement(s.iftrue),
                    self.statement(s.iffalse),
                ],
            )
        if isinstance(s, (c.While, c.For, c.DoWhile)):
            self.loop_depth += 1
            self.scopes.append({})
            if isinstance(s, c.For):
                init = self.statement(s.init)
                cond = (
                    self.scalar(s, self.expr(s.cond))
                    if s.cond
                    else Node("const", INT, value=1)
                )
                step = self.statement(s.next)
                body = self.statement(s.stmt)
                n = self.node(s, "for", children=[init, cond, step, body])
            else:
                n = self.node(
                    s,
                    "do" if isinstance(s, c.DoWhile) else "while",
                    children=[
                        self.scalar(s, self.expr(s.cond)),
                        self.statement(s.stmt),
                    ],
                )
            self.scopes.pop()
            self.loop_depth -= 1
            return n
        if isinstance(s, (c.Break, c.Continue)):
            if not self.loop_depth:
                self.fail(s, "break/continue outside loop")
            return self.node(s, "break" if isinstance(s, c.Break) else "continue")
        if isinstance(s, c.EmptyStatement):
            return self.node(s, "block")
        return self.node(s, "expression", children=[self.expr(s)])

    @staticmethod
    def normalize_constant(value, type_):
        if isinstance(value, tuple):
            return value
        if type_.kind not in ("int", "pointer"):
            raise CompileError("constant requires an integer or pointer type")
        bits = type_.size * 8
        value &= (1 << bits) - 1
        if type_.signed and value & (1 << (bits - 1)):
            value -= 1 << bits
        return value

    def static_value(self, n):
        """Evaluate typed constant expressions and symbolic address relocations.

        Apply target-width conversions at every operation, not just at the final
        data store. This matters for (signed char)255 and unsigned wraparound.
        """
        if n.op == "cast":
            return self.normalize_constant(self.static_value(n.children[0]), n.type)
        if n.op == "const":
            return self.normalize_constant(n.value, n.type)
        if n.op == "address":
            value = n.children[0]
            if value.op == "var" and value.value.storage in ("global", "function"):
                return (value.value.key, 0)
            if value.op == "deref":
                return self.static_value(value.children[0])
        if n.op == "pointer_add":
            base = self.static_value(n.children[0])
            delta = self.static_value(n.children[1])
            if isinstance(base, tuple) and isinstance(delta, int):
                return (base[0], base[1] + delta * n.value)
        if n.op == "select":
            cond = self.static_value(n.children[0])
            if isinstance(cond, int):
                return self.static_value(n.children[1 if cond else 2])
        if n.op == "unary":
            value = self.static_value(n.children[0])
            if isinstance(value, int):
                value = {
                    "-": lambda: -value,
                    "~": lambda: ~value,
                    "!": lambda: int(not value),
                }[n.value]()
                return self.normalize_constant(value, n.type)
        if n.op == "binary":
            a = self.static_value(n.children[0])
            if n.value == "&&" and a == 0:
                return 0
            if n.value == "||" and isinstance(a, int) and a != 0:
                return 1
            b = self.static_value(n.children[1])
            if isinstance(a, int) and isinstance(b, int):

                def quotient():
                    return (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)

                operations = {
                    "+": lambda: a + b,
                    "-": lambda: a - b,
                    "*": lambda: a * b,
                    "/": quotient,
                    "%": lambda: a - quotient() * b,
                    "<<": lambda: a << b,
                    ">>": lambda: a >> b,
                    "&": lambda: a & b,
                    "|": lambda: a | b,
                    "^": lambda: a ^ b,
                    "==": lambda: int(a == b),
                    "!=": lambda: int(a != b),
                    "<": lambda: int(a < b),
                    "<=": lambda: int(a <= b),
                    ">": lambda: int(a > b),
                    ">=": lambda: int(a >= b),
                    "&&": lambda: int(bool(a) and bool(b)),
                    "||": lambda: int(bool(a) or bool(b)),
                }
                if n.value in ("<<", ">>") and not 0 <= b < 32:
                    raise CompileError(f"{n.location}: invalid constant shift count")
                return self.normalize_constant(operations[n.value](), n.type)
        raise CompileError(f"{n.location}: unsupported static constant initializer")

    def build(self, tree):
        definitions = {}
        for item in tree.ext:
            if isinstance(item, c.Typedef):
                self.typedefs[item.name] = self.typename(item)
                continue
            d = item.decl if isinstance(item, c.FuncDef) else item
            if not isinstance(d, c.Decl) or not d.name:
                self.fail(d, "unsupported top-level declaration")
            t = (
                self.resolve_array(d, self.typename(d))
                if not isinstance(d.type, c.FuncDecl)
                else self.typename(d)
            )
            storage = "function" if t.kind == "function" else "global"
            if any(x not in ("static", "extern") for x in d.storage):
                self.fail(d, "unsupported storage specifier")
            old = self.scopes[0].get(d.name)
            if old and old.type != t:
                self.fail(d, "conflicting declaration")
            sym = old or self.new(d.name, t, storage)
            self.scopes[0][d.name] = sym
            if isinstance(item, c.FuncDef):
                if d.name in definitions:
                    self.fail(d, "duplicate function definition")
                definitions[d.name] = item
            elif storage == "global" and "extern" not in d.storage:
                if any(g.symbol.name == d.name for g in self.globals):
                    self.fail(d, "duplicate global definition")
                if not t.size:
                    self.fail(d, "global requires a complete object type")
                self.globals.append(Global(sym, bytearray(t.size)))
        # All global/function names are now available to initializers and bodies.
        for item in tree.ext:
            if isinstance(item, c.Decl) and item.init is not None:
                sym = self.scopes[0][item.name]
                g = next((g for g in self.globals if g.symbol == sym), None)
                if g is None:
                    self.fail(item, "extern initializers are unsupported")
                for offset, t, n in self.initializer(item, sym.type, item.init):
                    try:
                        value = self.static_value(n)
                    except (ZeroDivisionError, ValueError):
                        self.fail(item, "invalid static initializer")
                    if isinstance(value, tuple):
                        if t.size != 4:
                            self.fail(
                                item, "address initializer needs a 32-bit destination"
                            )
                        g.relocations.append((offset, *value))
                    else:
                        g.data[offset : offset + t.size] = (
                            value & ((1 << (8 * t.size)) - 1)
                        ).to_bytes(t.size, "big")
        for name, item in definitions.items():
            sym = self.scopes[0][name]
            self.locals = []
            self.scopes.append({})
            params = []
            declarations = item.decl.type.args.params if item.decl.type.args else []
            for d, t in zip(declarations, sym.type.params):
                if not d.name:
                    self.fail(d, "definition parameters need names")
                if d.name in self.scopes[-1]:
                    self.fail(d, "duplicate parameter")
                p = self.new(d.name, t, "parameter")
                params.append(p)
                self.scopes[-1][d.name] = p
            self.return_type = sym.type.base
            body = self.node(
                item.body,
                "block",
                children=[self.statement(x) for x in item.body.block_items or []],
            )
            self.functions.append(Function(sym, params, self.locals, body))
            self.scopes.pop()
        main = self.scopes[0].get("main")
        if not main or "main" not in definitions:
            raise CompileError("a definition of main is required")
        if main.type.params or main.type.base != INT:
            raise CompileError("entry point must be int main(void)")
        return Program(self.globals, self.functions)


def typecheck(tree):
    return Frontend().build(tree)
