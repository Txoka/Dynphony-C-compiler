#!/usr/bin/env python3
"""Build and exercise the first compiler written in Dynphony C."""

import argparse
from pathlib import Path

from dynphony import compile_sources
from dynphony.emulator import Machine, native_available, native_run


ROOT = Path(__file__).resolve().parents[2]
SELFHOST = ROOT / "selfhost"
STAGE0_SOURCES = (
    "src/main.c",
    "src/compiler.c",
    "src/frontend/lexer.c",
    "src/frontend/parser.c",
    "src/frontend/sema.c",
    "src/middle/lower.c",
    "src/middle/optimize.c",
    "src/target/dynphony/backend.c",
)


def build_stage0():
    sources = [
        (str(SELFHOST / name), (SELFHOST / name).read_text())
        for name in STAGE0_SOURCES
    ]
    return compile_sources(sources, include_dirs=[str(SELFHOST / "include")])


def run_machine(machine, halt, max_steps):
    if native_available():
        return native_run(machine, halt, max_steps=max_steps)
    return machine.run(halt, max_steps=max_steps)


def main():
    parser = argparse.ArgumentParser(
        description="build the C stage-0 compiler and compile one tiny C program"
    )
    parser.add_argument("source", nargs="?", default=SELFHOST / "examples/answer.c", type=Path)
    parser.add_argument("-o", "--output", default=SELFHOST / "build/answer.bin", type=Path)
    parser.add_argument(
        "--compiler-output",
        default=SELFHOST / "build/dyncc-stage0.bin",
        type=Path,
    )
    parser.add_argument("--max-steps", type=int, default=50_000_000)
    parser.add_argument("--no-run", action="store_true")
    args = parser.parse_args()

    compiler = build_stage0()
    args.compiler_output.parent.mkdir(parents=True, exist_ok=True)
    args.compiler_output.write_bytes(compiler.image.binary)

    source = args.source.read_bytes()
    machine = Machine(compiler.image.binary, inputs=[len(source), *source])
    status = run_machine(machine, compiler.image.symbols["_halt"], args.max_steps)
    if status:
        raise SystemExit(f"stage-0 compiler failed with status {status}")
    if any(value > 255 for value in machine.outputs):
        raise SystemExit("stage-0 compiler emitted a value that is not a byte")

    binary = bytes(machine.outputs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(binary)
    print(
        f"built {args.compiler_output} ({len(compiler.image.binary)} bytes); "
        f"wrote {args.output} ({len(binary)} bytes)"
    )

    if not args.no_run:
        program = Machine(binary)
        result = program.run(12)
        print(f"generated program returned {result} ({program.steps} instructions)")


if __name__ == "__main__":
    main()
