"""Language/frontend-independent pipeline orchestration and public C shortcut."""

from dataclasses import dataclass
from .frontends.c import CFrontend
from .frontends.protocol import SourceFrontend
from .middle.passes import optimize
from .targets.dynphony import generate, Target
from .targets.dynphony.legalize import legalize_runtime_arithmetic


@dataclass
class Compilation:
    parsed: object
    typed: object
    ir: object
    image: object


class Compiler:
    def __init__(self, frontend: SourceFrontend, target=None):
        self.frontend = frontend
        self.target = target or Target()

    def compile(self, source: str, filename: str = "<input>") -> Compilation:
        frontend = self.frontend.lower(source, filename)
        ir = optimize(frontend.ir)
        legalize_runtime_arithmetic(ir)
        ir = optimize(ir)
        return Compilation(
            frontend.parsed,
            frontend.typed,
            ir,
            generate(ir, self.target),
        )


def compile_source(source, filename="<input>", target=None):
    return Compiler(CFrontend(), target).compile(source, filename)
