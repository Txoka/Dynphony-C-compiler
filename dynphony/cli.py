"""Command-line interface; writes a raw image and optional IR/symbol map."""

import argparse
import json
import sys
import time
from pathlib import Path
from .compiler import compile_sources
from .targets.dynphony import Target
from .middle.model import CompileError
from .emulator import Machine, native_available, native_run, signed


def number(s):
    return int(s, 0)


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="dyncc", description="Compile a C subset to a Dynphony raw binary"
    )
    p.add_argument("source", type=Path, nargs="+")
    p.add_argument("-o", "--output", type=Path, default=Path("a.bin"))
    p.add_argument(
        "-I", dest="include_dirs", action="append", type=Path, default=[],
        help="add a header search directory",
    )
    p.add_argument(
        "-D", dest="defines", action="append", default=[], metavar="NAME[=VALUE]",
        help="define a preprocessor macro",
    )
    p.add_argument("--pic", action="store_true")
    p.add_argument("--load-address", type=number, default=0)
    p.add_argument("--ram-size", type=number, default=16 * 1024 * 1024)
    p.add_argument("--persistent-size", type=number, default=0)
    p.add_argument(
        "--include-framebuffer",
        action="store_true",
        help="serialize the text framebuffer as zero bytes instead of clearing reserved RAM at startup",
    )
    p.add_argument("--emit-ir", type=Path)
    p.add_argument("--map", type=Path)
    p.add_argument(
        "--run", action="store_true", help="run the binary in the reference emulator"
    )
    p.add_argument(
        "--run-address", type=number, help="relocate a PIC image for emulator execution"
    )
    p.add_argument("--max-steps", type=int, default=5_000_000)
    p.add_argument(
        "--hz-meter",
        action="store_true",
        help="show live emulator throughput in instructions per second",
    )
    p.add_argument(
        "--engine",
        choices=("auto", "native", "python"),
        default="auto",
        help="emulator engine (default: native when installed, otherwise python)",
    )
    args = p.parse_args(argv)
    try:
        target = Target(
            args.ram_size,
            args.persistent_size,
            args.load_address,
            args.pic,
            args.include_framebuffer,
        )
        sources = [(str(path), path.read_text()) for path in args.source]
        result = compile_sources(
            sources,
            target,
            include_dirs=args.include_dirs,
            defines=args.defines,
        )
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
            if args.engine == "native" and not native_available():
                raise CompileError(
                    "native emulator is unavailable; run 'make native' or use "
                    "--engine python"
                )
            engine = (
                "native"
                if args.engine == "native"
                or (args.engine == "auto" and native_available())
                else "python"
            )
            started = time.perf_counter()
            meter_width = 0
            meter_steps = -1

            def show_meter(machine):
                nonlocal meter_steps, meter_width
                if machine.steps == meter_steps:
                    return
                meter_steps = machine.steps
                elapsed = time.perf_counter() - started
                hz = machine.steps / elapsed if elapsed else 0
                message = (
                    f"Emulating: {machine.steps:,} instructions; "
                    f"{hz:,.0f} Hz; PC={machine.pc:#x}"
                )
                print(
                    "\r" + message.ljust(meter_width),
                    end="",
                    flush=True,
                )
                meter_width = max(meter_width, len(message))

            try:
                runner = native_run if engine == "native" else m.run
                if engine == "native":
                    value = runner(
                        m,
                        halt,
                        args.max_steps,
                        progress=show_meter if args.hz_meter else None,
                    )
                else:
                    value = runner(
                        halt,
                        args.max_steps,
                        progress=show_meter if args.hz_meter else None,
                    )
            finally:
                if args.hz_meter:
                    show_meter(m)
                    print(flush=True)
            print(
                f"main returned {signed(value)} (r1=0x{value:08x}); "
                f"{m.steps} instructions; {engine} engine"
            )
        return 0
    except (CompileError, OSError, RuntimeError, ValueError) as exc:
        print(f"dyncc: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
