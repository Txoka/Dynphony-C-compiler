"""Command-line interface; writes a raw image and optional IR/symbol map."""

import argparse
import json
import sys
from pathlib import Path
from .compiler import compile_source
from .backend import Target
from .model import CompileError
from .emulator import Machine, signed


def number(s):
    return int(s, 0)


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="dyncc", description="Compile a C subset to a Dynphony raw binary"
    )
    p.add_argument("source", type=Path)
    p.add_argument("-o", "--output", type=Path, default=Path("a.bin"))
    p.add_argument("--pic", action="store_true")
    p.add_argument("--load-address", type=number, default=0)
    p.add_argument("--ram-size", type=number, default=16 * 1024 * 1024)
    p.add_argument("--persistent-size", type=number, default=0)
    p.add_argument("--emit-ir", type=Path)
    p.add_argument("--map", type=Path)
    p.add_argument(
        "--run", action="store_true", help="run the binary in the reference emulator"
    )
    p.add_argument(
        "--run-address", type=number, help="relocate a PIC image for emulator execution"
    )
    p.add_argument("--max-steps", type=int, default=5_000_000)
    args = p.parse_args(argv)
    try:
        target = Target(
            args.ram_size, args.persistent_size, args.load_address, args.pic
        )
        result = compile_source(args.source.read_text(), str(args.source), target)
        if (
            args.run_address is not None
            and not args.pic
            and args.run_address != args.load_address
        ):
            raise CompileError(
                "--run-address may differ from --load-address only with --pic"
            )
        args.output.write_bytes(result.image.binary)
        if args.emit_ir:
            args.emit_ir.write_text(result.ir.dump())
        if args.map:
            args.map.write_text(json.dumps(result.image.metadata(), indent=2) + "\n")
        print(f"Wrote {len(result.image.binary)} bytes to {args.output}")
        if args.run:
            address = (
                args.run_address if args.run_address is not None else args.load_address
            )
            m = Machine(
                result.image.binary,
                args.ram_size,
                address,
                persistent_size=args.persistent_size,
            )
            halt = result.image.symbols["_halt"] + (address if args.pic else 0)
            value = m.run(halt, args.max_steps)
            print(
                f"main returned {signed(value)} (r1=0x{value:08x}); {m.steps} instructions"
            )
        return 0
    except (CompileError, OSError, RuntimeError, ValueError) as exc:
        print(f"dyncc: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
