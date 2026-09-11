"""C frontend public surface."""

from .frontend import typecheck
from .parser import parse, strip_comments
from .compiler import CFrontend

__all__ = ["CFrontend", "parse", "strip_comments", "typecheck"]
