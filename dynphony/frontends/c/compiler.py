"""C-specific source preparation and lowering into common IR."""

import re

from pycparser import c_ast

from ..protocol import FrontendResult
from ...middle.ir import lower
from ...middle.model import CompileError
from ...runtime import NAMES as INTRINSIC_NAMES, PROTOTYPES, SOURCE
from .frontend import typecheck
from .parser import parse, strip_comments


class CFrontend:
    def lower(self, source: str, filename: str = "<input>") -> FrontendResult:
        if re.search(r"\b__dyn_\w*", strip_comments(source)):
            raise CompileError("identifiers beginning __dyn_ are reserved for the runtime")
        parsed = parse(source, filename)
        for item in parsed.ext:
            if isinstance(item, c_ast.FuncDef) and item.decl.name in INTRINSIC_NAMES:
                raise CompileError(
                    f"{item.decl.name} is a reserved Dynphony intrinsic"
                )
        intrinsics = parse(PROTOTYPES, "<dynphony-intrinsics>")
        runtime = parse(SOURCE, "<dynphony-runtime>")
        parsed.ext = intrinsics.ext + runtime.ext + parsed.ext
        typed = typecheck(parsed)
        return FrontendResult(parsed, typed, lower(typed))


__all__ = ["CFrontend"]
