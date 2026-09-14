"""C source preparation and pycparser adapter."""

import re

from pycparser import c_parser

from ...middle.model import CompileError


def strip_comments(source):
    """Remove comments while preserving literals, spacing, and line numbers."""
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*'
    return re.sub(
        pattern,
        lambda match: (
            re.sub(r"[^\n]", " ", match[0])
            if match[0].startswith(("/*", "//"))
            else match[0]
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


__all__ = ["parse", "strip_comments"]
