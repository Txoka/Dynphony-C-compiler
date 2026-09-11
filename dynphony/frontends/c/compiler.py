"""C-specific source preparation and lowering into common IR."""

import re

from pycparser import c_ast

from ..protocol import FrontendResult
from ...middle.ir import lower
from ...middle.model import CHAR, INT, UINT, CompileError, Global, Program, Symbol, array
from ...runtime import (
    NAMES as INTRINSIC_NAMES,
    PROTOTYPES,
    LIBRARY_PROTOTYPES,
    SCREEN_SOURCE,
    SOURCE,
    TEXT_SCREEN_NAMES,
)
from .frontend import typecheck
from .parser import parse, strip_comments
from .preprocessor import Preprocessor


def link_programs(programs):
    """Resolve independently checked translation units into one typed program."""
    globals_ = []
    functions = []
    symbols = {}
    definitions = {}
    for program in programs:
        for key, symbol in program.symbols.items():
            previous = symbols.get(key)
            if previous is not None and previous.type != symbol.type:
                raise CompileError(f"conflicting declarations of {symbol.name}")
            symbols.setdefault(key, symbol)
        for global_ in program.globals:
            key = global_.symbol.key
            if key in definitions:
                raise CompileError(f"multiple definitions of {global_.symbol.name}")
            definitions[key] = global_.symbol
            globals_.append(global_)
        for function in program.functions:
            key = function.symbol.key
            if key in definitions:
                raise CompileError(f"multiple definitions of {function.symbol.name}")
            definitions[key] = function.symbol
            functions.append(function)
    main = definitions.get("main")
    if main is None or main.storage != "function":
        raise CompileError("a definition of main is required")
    if main.type.params or main.type.base != INT:
        raise CompileError("entry point must be int main(void)")
    return Program(globals_, functions, symbols)


class CFrontend:
    def __init__(self, include_dirs=(), defines=()):
        self.include_dirs = tuple(include_dirs)
        self.defines = tuple(defines)

    @staticmethod
    def uses_text_screen(tree):
        def visit(node):
            if isinstance(node, c_ast.FuncCall) and isinstance(node.name, c_ast.ID):
                if node.name.name in TEXT_SCREEN_NAMES:
                    return True
            return any(visit(child) for _, child in node.children())

        return visit(tree)

    def lower(self, source: str, filename: str = "<input>") -> FrontendResult:
        return self.lower_project([(filename, source)])

    def lower_project(self, sources) -> FrontendResult:
        parsed_units = []
        programs = []
        for index, (filename, source) in enumerate(sources):
            if re.search(r"\b__dyn_\w*", strip_comments(source)):
                raise CompileError("identifiers beginning __dyn_ are reserved for the runtime")
            processor = Preprocessor(self.include_dirs, self.defines)
            processed = processor.process(source, filename)
            parsed = parse(
                "typedef _Bool bool;\n" + PROTOTYPES + LIBRARY_PROTOTYPES + processed,
                filename,
            )
            parsed_units.append(parsed)
            programs.append(typecheck(parsed, require_main=False, namespace=str(index)))
        for parsed in parsed_units:
            for item in parsed.ext:
                if isinstance(item, c_ast.FuncDef) and item.decl.name in (
                    INTRINSIC_NAMES | TEXT_SCREEN_NAMES
                ):
                    raise CompileError(
                        f"{item.decl.name} is a reserved Dynphony intrinsic"
                    )

        uses_screen = any(self.uses_text_screen(tree) for tree in parsed_units)
        runtime_source = "typedef _Bool bool;\n" + PROTOTYPES + LIBRARY_PROTOTYPES + SOURCE
        if uses_screen:
            runtime_source += SCREEN_SOURCE
        runtime_tree = parse(runtime_source, "<dynphony-runtime>")
        programs.append(typecheck(runtime_tree, require_main=False, namespace="runtime"))
        typed = link_programs(programs)

        if uses_screen:
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
        typed.globals.append(
            Global(
                Symbol("__dyn_heap_anchor", array(CHAR, 7), "global", "__dyn_heap_anchor"),
                bytearray(),
                reserved=7,
            )
        )
        parsed = parsed_units[0] if len(parsed_units) == 1 else parsed_units
        return FrontendResult(parsed, typed, lower(typed))


__all__ = ["CFrontend"]
