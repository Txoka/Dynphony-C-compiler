"""Deterministic fixed-point scheduling for middle-end transformations."""

from collections.abc import Callable, Iterable

from ..ir import ModuleIR

ModulePass = Callable[[ModuleIR], object]
Snapshot = Callable[[ModuleIR], object]


class FixedPointPassManager:
    def __init__(
        self,
        passes: Iterable[ModulePass],
        snapshot: Snapshot,
        max_iterations: int = 100,
    ):
        self.passes = tuple(passes)
        self.snapshot = snapshot
        self.max_iterations = max_iterations

    def run(self, module: ModuleIR) -> ModuleIR:
        for _ in range(self.max_iterations):
            before = self.snapshot(module)
            for pass_ in self.passes:
                pass_(module)
            if self.snapshot(module) == before:
                return module
        raise AssertionError("optimizer failed to reach a fixed point")


__all__ = ["FixedPointPassManager", "ModulePass"]
