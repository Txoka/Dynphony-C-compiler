#!/usr/bin/env python3
"""Build and exercise the first compiler written in Dynphony C."""

import argparse
from pathlib import Path

from dynphony import compile_sources
from dynphony.emulator import Machine, native_available, native_run
from dynphony.project import (
    Project,
    ProjectFile,
    decode_control,
    make_persistent_image,
)


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


def run_stage0(compiler, source, load_address=8192, persistent_size=1 << 16):
    project = Project((ProjectFile("main.c", bytes(source)),))
    persistent = make_persistent_image(
        project,
        persistent_size=persistent_size,
        program_load_address=load_address,
    )
    machine = Machine(compiler.image.binary, persistent_size=persistent_size)
    machine.persistent[:] = persistent
    status = run_machine(machine, compiler.image.symbols["_halt"], 50_000_000)
    control = decode_control(machine.persistent)
    record = machine.persistent[
        control.output_address:
        control.output_address + control.output_byte_length
    ]
    if status or control.status:
        return status, machine, b"", control
    image_length = int.from_bytes(record[:4], "big")
    return status, machine, bytes(record[4:4 + image_length]), control


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
    status, machine, binary, control = run_stage0(compiler, source)
    if status:
        raise SystemExit(f"stage-0 compiler failed with status {status}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(binary)
    print(
        f"built {args.compiler_output} ({len(compiler.image.binary)} bytes); "
        f"wrote {args.output} ({len(binary)} bytes)"
    )

    if not args.no_run:
        program = Machine(binary, load_address=control.program_load_address)
        halt_offset = 12 if len(binary) == 16 else 24
        result = program.run(control.program_load_address + halt_offset)
        print(f"generated program returned {result} ({program.steps} instructions)")


if __name__ == "__main__":
    main()
