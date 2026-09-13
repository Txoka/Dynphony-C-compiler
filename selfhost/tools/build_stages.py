#!/usr/bin/env python3
"""Build the two self-hosted compiler stages from an existing stage 0."""

import argparse
from pathlib import Path

from dynphony.emulator import Machine, native_available, native_run
from dynphony.project import decode_control, make_persistent_image, project_from_directory


ROOT = Path(__file__).resolve().parents[2]
SELFHOST = ROOT / "selfhost"


def compile_stage(compiler, persistent, load_address, max_steps, symphony):
    machine = Machine(
        compiler,
        load_address=load_address,
        persistent_size=len(persistent),
        symphony=symphony,
    )
    machine.persistent[:] = persistent
    if native_available(symphony):
        result = native_run(machine, None, max_steps=max_steps)
    else:
        result = machine.run(None, max_steps=max_steps)
    control = decode_control(machine.persistent)
    if result or control.status:
        raise RuntimeError(
            f"compiler failed: return={result}, status={control.status}"
        )
    record = machine.persistent[control.output_address:]
    length = int.from_bytes(record[:4], "big")
    if length > control.output_byte_length - 4:
        raise RuntimeError("compiler produced a truncated executable record")
    return bytes(record[4:4 + length]), machine.steps


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage0", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--persistent-size", type=lambda value: int(value, 0),
                        default=1 << 24)
    parser.add_argument("--load-address", type=lambda value: int(value, 0),
                        default=0)
    parser.add_argument("--max-steps", type=int)
    parser.add_argument(
        "--target", choices=("dynphony", "symphony"), default="dynphony"
    )
    args = parser.parse_args(argv)
    max_steps = args.max_steps or (
        2_000_000_000 if args.target == "symphony" else 1_200_000_000
    )

    project = project_from_directory(
        SELFHOST, exclude=("build", "examples", "tests")
    )
    persistent = make_persistent_image(
        project,
        persistent_size=args.persistent_size,
        program_load_address=args.load_address,
        symphony=args.target == "symphony",
    )
    stage0 = args.stage0.read_bytes()
    stage1, steps1 = compile_stage(
        stage0,
        persistent,
        args.load_address,
        max_steps,
        args.target == "symphony",
    )
    stage2, steps2 = compile_stage(
        stage1,
        persistent,
        args.load_address,
        max_steps,
        args.target == "symphony",
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stage1_path = args.output_dir / "dyncc-stage1.bin"
    stage2_path = args.output_dir / "dyncc-stage2.bin"
    stage1_path.write_bytes(stage1)
    stage2_path.write_bytes(stage2)
    print(f"Wrote {len(stage1)} bytes to {stage1_path} ({steps1} instructions)")
    print(f"Wrote {len(stage2)} bytes to {stage2_path} ({steps2} instructions)")
    if stage1 != stage2:
        raise RuntimeError("stage 1 and stage 2 differ")
    print("stage 1 and stage 2 are byte-identical")


if __name__ == "__main__":
    main()
