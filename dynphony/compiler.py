"""Public pipeline API. Intermediate representations remain inspectable."""

import re
from dataclasses import dataclass
from pycparser import c_ast
from .frontend import parse, typecheck, strip_comments
from .ir import lower
from .optimizer import optimize
from .backend import generate, Target
from .intrinsics import NAMES as INTRINSIC_NAMES, PROTOTYPES
from .model import CompileError
from .runtime import SOURCE


@dataclass
class Compilation:
    parsed: object
    typed: object
    ir: object
    image: object


def compile_source(source, filename="<input>", target=None):
    if re.search(r"\b__dyn_\w*", strip_comments(source)):
        raise CompileError("identifiers beginning __dyn_ are reserved for the runtime")
    parsed = parse(source, filename)
    for item in parsed.ext:
        if isinstance(item, c_ast.FuncDef) and item.decl.name in INTRINSIC_NAMES:
            raise CompileError(f"{item.decl.name} is a reserved Dynphony intrinsic")
    intrinsics = parse(PROTOTYPES, "<dynphony-intrinsics>")
    runtime = parse(SOURCE, "<dynphony-runtime>")
    parsed.ext = intrinsics.ext + runtime.ext + parsed.ext
    typed = typecheck(parsed)
    ir = optimize(lower(typed))
    return Compilation(parsed, typed, ir, generate(ir, target or Target()))
