"""Contract between source-language frontends and the common middle end."""

from dataclasses import dataclass
from typing import Protocol

from ..middle.ir import ModuleIR


@dataclass
class FrontendResult:
    parsed: object
    typed: object
    ir: ModuleIR


class SourceFrontend(Protocol):
    def lower(self, source: str, filename: str = "<input>") -> FrontendResult:
        """Parse and type-check source, then produce canonical common IR."""


__all__ = ["FrontendResult", "SourceFrontend"]
