"""Small, deterministic C preprocessor used by the freestanding frontend.

It intentionally implements the project-oriented core rather than depending on
the host C compiler: includes, object/function macros, and integer conditional
compilation.  Expansion is token based, so strings and character constants are
never rewritten accidentally.
"""

from __future__ import annotations

import ast
import re
from dataclasses import dataclass
from pathlib import Path

from ...middle.model import CompileError
from .parser import strip_comments


BUILTIN_HEADERS = {
    "stdbool.h": (
        "#pragma once\n#define bool _Bool\n#define true 1\n#define false 0\n"
    ),
    "stddef.h": (
        "#pragma once\ntypedef unsigned int size_t; typedef int ptrdiff_t;\n"
        "#define NULL ((void *)0)\n"
    ),
    "stdint.h": (
        "#pragma once\ntypedef signed char int8_t; typedef unsigned char uint8_t; "
        "typedef short int16_t; typedef unsigned short uint16_t; "
        "typedef int int32_t; typedef unsigned int uint32_t;\n"
    ),
    "stdlib.h": (
        "#pragma once\n#include <stddef.h>\n"
        "void *malloc(size_t); void free(void *); "
        "void *calloc(size_t,size_t); void *realloc(void *,size_t);\n"
    ),
    "string.h": (
        "#pragma once\n#include <stddef.h>\n"
        "void *memcpy(void *,const void *,size_t); "
        "void *memmove(void *,const void *,size_t); "
        "void *memset(void *,int,size_t); "
        "int memcmp(const void *,const void *,size_t);\n"
    ),
    "stdio.h": (
        "#pragma once\nint printf(const char *format);\n"
    ),
    "dynphony.h": (
        "#pragma once\n"
        "unsigned int input(void); void output(unsigned int); "
        "unsigned int keyboard(void); "
        "void screen(unsigned int,unsigned int); "
        "unsigned int time(void); unsigned int time_low(void); "
        "unsigned int time_high(void); "
        "unsigned int persistent_load(unsigned int); "
        "void persistent_store(unsigned int,unsigned int); "
        "char *screen_framebuffer(void); "
        "void screen_cursor(unsigned int,unsigned int);\n"
    ),
}


TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
    r"[A-Za-z_]\w*|"
    r"(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?[fFlL]?|"
    r"\d+[eE][+-]?\d+[fFlL]?|"
    r"0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*|"
    r">>=|<<=|\+\+|--|->|\+=|-=|\*=|/=|%=|&=|\|=|\^=|"
    r"==|!=|<=|>=|&&|\|\||<<|>>|##|\S"
)


@dataclass
class Macro:
    parameters: tuple[str, ...] | None
    replacement: tuple[str, ...]


def _tokens(text):
    return TOKEN.findall(text)


class Preprocessor:
    def __init__(self, include_dirs=(), defines=()):
        self.include_dirs = [Path(path) for path in include_dirs]
        self.macros: dict[str, Macro] = {}
        self.once: set[Path] = set()
        self.active_files: list[Path] = []
        for definition in defines:
            name, separator, value = definition.partition("=")
            if not re.fullmatch(r"[A-Za-z_]\w*", name):
                raise CompileError(f"invalid macro name: {name}")
            self.macros[name] = Macro(None, tuple(_tokens(value if separator else "1")))

    def expand(self, tokens, disabled=frozenset()):
        out = []
        i = 0
        while i < len(tokens):
            name = tokens[i]
            macro = self.macros.get(name)
            if macro is None or name in disabled:
                out.append(name)
                i += 1
                continue
            if macro.parameters is None:
                out.extend(self.expand(list(macro.replacement), disabled | {name}))
                i += 1
                continue
            if i + 1 >= len(tokens) or tokens[i + 1] != "(":
                out.append(name)
                i += 1
                continue
            arguments = []
            current = []
            depth = 0
            j = i + 2
            while j < len(tokens):
                token = tokens[j]
                if token == "(" :
                    depth += 1
                    current.append(token)
                elif token == ")" and depth:
                    depth -= 1
                    current.append(token)
                elif token == ")":
                    arguments.append(current)
                    break
                elif token == "," and depth == 0:
                    arguments.append(current)
                    current = []
                else:
                    current.append(token)
                j += 1
            else:
                raise CompileError(f"unterminated invocation of macro {name}")
            if arguments == [[]] and not macro.parameters:
                arguments = []
            if len(arguments) != len(macro.parameters):
                raise CompileError(
                    f"macro {name} expects {len(macro.parameters)} arguments, "
                    f"got {len(arguments)}"
                )
            replacements = {
                parameter: self.expand(argument, disabled)
                for parameter, argument in zip(macro.parameters, arguments)
            }
            body = []
            for token in macro.replacement:
                body.extend(replacements.get(token, [token]))
            out.extend(self.expand(body, disabled | {name}))
            i = j + 1
        return out

    def condition(self, text):
        tokens = _tokens(text)
        replaced = []
        i = 0
        while i < len(tokens):
            if tokens[i] == "defined":
                if i + 1 < len(tokens) and tokens[i + 1] == "(":
                    if i + 3 >= len(tokens) or tokens[i + 3] != ")":
                        raise CompileError("malformed defined() expression")
                    name, i = tokens[i + 2], i + 4
                elif i + 1 < len(tokens):
                    name, i = tokens[i + 1], i + 2
                else:
                    raise CompileError("malformed defined expression")
                replaced.append("1" if name in self.macros else "0")
            else:
                replaced.append(tokens[i])
                i += 1
        tokens = self.expand(replaced)
        expression = []
        for token in tokens:
            if re.fullmatch(r"[A-Za-z_]\w*", token):
                expression.append("0")
            elif re.fullmatch(r"(?:0[xX][0-9A-Fa-f]+|\d+)[uUlL]*", token):
                expression.append(re.sub(r"[uUlL]+$", "", token))
            elif token == "&&":
                expression.append(" and ")
            elif token == "||":
                expression.append(" or ")
            elif token == "!":
                expression.append(" not ")
            elif token == "/":
                expression.append("//")
            else:
                expression.append(token)
        try:
            tree = ast.parse("".join(expression), mode="eval")
        except SyntaxError as exc:
            raise CompileError(f"invalid preprocessor expression: {text}") from exc
        allowed = (
            ast.Expression, ast.Constant, ast.UnaryOp, ast.BinOp, ast.BoolOp,
            ast.Compare, ast.Not, ast.Invert, ast.USub, ast.UAdd, ast.Add,
            ast.Sub, ast.Mult, ast.Div, ast.FloorDiv, ast.Mod, ast.LShift,
            ast.RShift, ast.BitAnd, ast.BitOr, ast.BitXor, ast.And, ast.Or,
            ast.Eq, ast.NotEq, ast.Lt, ast.LtE, ast.Gt, ast.GtE,
        )
        if any(not isinstance(node, allowed) for node in ast.walk(tree)):
            raise CompileError(f"unsupported preprocessor expression: {text}")
        try:
            return bool(eval(compile(tree, "<preprocessor>", "eval"), {"__builtins__": {}}, {}))
        except (ArithmeticError, ValueError) as exc:
            raise CompileError(f"invalid preprocessor expression: {text}") from exc

    def find_include(self, name, quoted, current):
        candidates = ([current.parent] if quoted and current else []) + self.include_dirs
        for directory in candidates:
            path = (directory / name).resolve()
            if path.is_file():
                return path
        raise CompileError(f"include file not found: {name}")

    def process(self, source, filename="<input>"):
        path = None if filename.startswith("<") else Path(filename).resolve()
        identity = path or (filename if filename != "<input>" else None)
        if identity in self.once:
            return ""
        if identity in self.active_files:
            raise CompileError(f"recursive include: {identity}")
        if identity:
            self.active_files.append(identity)
        source = strip_comments(source)
        physical = source.splitlines(keepends=True)
        lines = []
        pending = ""
        for line in physical:
            pending += line
            if pending.rstrip("\r\n").endswith("\\"):
                pending = pending.rstrip("\r\n")[:-1]
            else:
                lines.append(pending)
                pending = ""
        if pending:
            lines.append(pending)

        output = []
        states = []
        active = True
        try:
            for line_number, line in enumerate(lines, 1):
                match = re.match(r"\s*#\s*([A-Za-z_]\w*)?(.*)$", line)
                if not match:
                    output.append(" ".join(self.expand(_tokens(line))) + "\n" if active else "\n")
                    continue
                directive, rest = match.group(1) or "", match.group(2).strip()
                if directive in ("if", "ifdef", "ifndef"):
                    parent = active
                    test = (
                        rest in self.macros if directive == "ifdef" else
                        rest not in self.macros if directive == "ifndef" else
                        self.condition(rest)
                    ) if parent else False
                    states.append([parent, bool(test), False])
                    active = parent and bool(test)
                elif directive == "elif":
                    if not states:
                        raise CompileError(f"{filename}:{line_number}: unmatched #elif")
                    parent, taken, _ = states[-1]
                    if states[-1][2]:
                        raise CompileError(f"{filename}:{line_number}: #elif after #else")
                    test = parent and not taken and self.condition(rest)
                    states[-1][1] = taken or bool(test)
                    active = bool(test)
                elif directive == "else":
                    if not states:
                        raise CompileError(f"{filename}:{line_number}: unmatched #else")
                    parent, taken, seen_else = states[-1]
                    if seen_else:
                        raise CompileError(f"{filename}:{line_number}: duplicate #else")
                    states[-1][2] = True
                    active = parent and not taken
                    states[-1][1] = True
                elif directive == "endif":
                    if not states:
                        raise CompileError(f"{filename}:{line_number}: unmatched #endif")
                    parent, _, _ = states.pop()
                    active = parent
                elif active and directive == "include":
                    include = re.fullmatch(r'(["<])([^">]+)[">]', rest)
                    if not include:
                        raise CompileError(f"{filename}:{line_number}: malformed #include")
                    name = include.group(2)
                    if include.group(1) == "<" and name in BUILTIN_HEADERS:
                        output.append(self.process(BUILTIN_HEADERS[name], f"<{name}>"))
                    else:
                        target = self.find_include(name, include.group(1) == '"', path)
                        output.append(self.process(target.read_text(), str(target)))
                elif active and directive == "define":
                    definition = re.match(r"([A-Za-z_]\w*)(.*)$", rest)
                    if not definition:
                        raise CompileError(f"{filename}:{line_number}: malformed #define")
                    name, tail = definition.groups()
                    parameters = None
                    if tail.startswith("("):
                        close = tail.find(")")
                        if close < 0:
                            raise CompileError(f"{filename}:{line_number}: malformed macro parameters")
                        raw = tail[1:close].strip()
                        parameters = tuple(x.strip() for x in raw.split(",")) if raw else ()
                        if any(not re.fullmatch(r"[A-Za-z_]\w*", x) for x in parameters):
                            raise CompileError(f"{filename}:{line_number}: invalid macro parameter")
                        tail = tail[close + 1:]
                    replacement = tuple(_tokens(tail))
                    if "#" in replacement or "##" in replacement:
                        raise CompileError("macro stringification and token pasting are not supported")
                    self.macros[name] = Macro(parameters, replacement)
                elif active and directive == "undef":
                    self.macros.pop(rest, None)
                elif active and directive == "pragma" and rest == "once":
                    if identity:
                        self.once.add(identity)
                elif active and directive == "error":
                    raise CompileError(f"{filename}:{line_number}: {rest or '#error'}")
                elif active and directive not in ("",):
                    raise CompileError(f"{filename}:{line_number}: unsupported directive #{directive}")
                output.append("\n")
            if states:
                raise CompileError(f"{filename}: unterminated conditional directive")
            return "".join(output)
        finally:
            if identity:
                self.active_files.pop()


def preprocess(source, filename="<input>", include_dirs=(), defines=(), processor=None):
    processor = processor or Preprocessor(include_dirs, defines)
    return processor.process(source, filename)


__all__ = ["Macro", "Preprocessor", "preprocess"]
