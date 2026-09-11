"""C-specific source preparation and lowering into common IR."""

import re

from pycparser import c_ast

from ..protocol import FrontendResult
from ...middle.ir import lower
from ...middle.model import CompileError
from ...middle.model import CHAR, UINT, Global, Symbol, array
from ...runtime import (
    NAMES as INTRINSIC_NAMES,
    PROTOTYPES,
    SCREEN_SOURCE,
    SOURCE,
    TEXT_SCREEN_NAMES,
)
from .frontend import typecheck
from .parser import parse, strip_comments


class CFrontend:
    @staticmethod
    def uses_text_screen(tree):
        def visit(node):
            if isinstance(node, c_ast.FuncCall) and isinstance(node.name, c_ast.ID):
                if node.name.name in TEXT_SCREEN_NAMES:
                    return True
            return any(visit(child) for _, child in node.children())

        return visit(tree)

    def lower(self, source: str, filename: str = "<input>") -> FrontendResult:
        if re.search(r"\b__dyn_\w*", strip_comments(source)):
            raise CompileError("identifiers beginning __dyn_ are reserved for the runtime")
        # This freestanding compiler has no <stdbool.h> or preprocessor. Make
        # the standard spelling available as a small language extension.
        parsed = parse("typedef _Bool bool;\n" + source, filename)
        for item in parsed.ext:
            if isinstance(item, c_ast.FuncDef) and item.decl.name in (
                INTRINSIC_NAMES | TEXT_SCREEN_NAMES
            ):
                raise CompileError(
                    f"{item.decl.name} is a reserved Dynphony intrinsic"
                )
        intrinsics = parse(PROTOTYPES, "<dynphony-intrinsics>")
        runtime = parse(SOURCE, "<dynphony-runtime>")
        screen_runtime = (
            parse(SCREEN_SOURCE, "<dynphony-text-screen>").ext
            if self.uses_text_screen(parsed)
            else []
        )
        parsed.ext = intrinsics.ext + runtime.ext + screen_runtime + parsed.ext
        typed = typecheck(parsed)
        if self.uses_text_screen(parsed):
            typed.globals.extend(
                [
                    Global(
                        Symbol("__dyn_printf_framebuffer", array(CHAR, 3840), "global", "__dyn_printf_framebuffer"),
                        bytearray(),
                        reserved=3840,
                    ),
                    Global(Symbol("__dyn_printf_cursor", UINT, "global", "__dyn_printf_cursor"), bytearray(4)),
                    Global(Symbol("__dyn_printf_column", UINT, "global", "__dyn_printf_column"), bytearray(4)),
                ]
            )
        return FrontendResult(parsed, typed, lower(typed))


__all__ = ["CFrontend"]
