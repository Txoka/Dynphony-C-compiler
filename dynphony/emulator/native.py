"""Optional native execution engine for the reference Machine state model."""

try:
    from . import _native
except ImportError:
    _native = None


def available():
    return _native is not None


def run(machine, halt_address=None, max_steps=5_000_000, progress=None,
        progress_interval=250_000):
    if _native is None:
        raise RuntimeError("native emulator extension is not installed")
    while machine.steps < max_steps:
        limit = max_steps
        if progress is not None:
            limit = min(limit, machine.steps + progress_interval)
        stopped, value = _native.run_chunk(machine, halt_address, limit)
        if stopped:
            return value
        if progress is not None:
            progress(machine)
    raise RuntimeError(
        f"execution limit exceeded ({max_steps} instructions), PC={machine.pc:#x}"
    )


__all__ = ["available", "run"]
