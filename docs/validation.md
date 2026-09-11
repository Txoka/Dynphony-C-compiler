# Validation record

Validated in the development environment on 2026-09-11 with Python 3.13 and pycparser 2.22.

- `python -m unittest discover -s tests -v`: **42 test methods passed**, including structures and natural layout, self-referential structure pointers, enums, `const`, local typedefs, static locals, register and stack-passed scalar arguments, the arena allocator, CFG reachability, the global Tier 1 fixed point, constant collapse, register allocation, tail calls, device intrinsics, and fixed/PIC termination. Final run: 4.097 seconds.
- Wheel build with `pip wheel --no-build-isolation --no-deps`: passed.
- Wheel installed into a separate directory; imported outside the source checkout and compiled/executed `6 * 7`: returned **42**.
- Fixed-address demonstration: **804 bytes**, returned **146**, 1,082 emulated instructions. Before the optimization passes it was 1,572 bytes and 2,195 instructions.
- PIC demonstration: **936 bytes**, loaded at `0x12345`, returned **146**, 1,302 emulated instructions. Before the optimization passes it was 1,708 bytes and 2,319 instructions.
- Towers of Hanoi demonstration: **348 bytes**, returned **0**, and emitted the expected 28 device outputs in **370 emulated instructions** for inputs `2, 0, 2, 1`. The original compiler output was 779 bytes and took 878 instructions.
- The focused constant-local example `int a=12345; int b=6789; return a+b;` is **8 bytes** and executes one instruction before reaching halt. Generic single-caller relocation absorbs `main`; the image contains one immediate constant instruction and one halt jump.
- `examples/constant_folding.c`, containing `return a + b * c` for constants 4, 34, and 43, is also **8 bytes**. It executes `mov r1, 1466` before halt and includes no multiplication helper.
- `examples/interprocedural_constant_folding.c` passes 4 from `main` into the sole-called `foo`, where 34 and 43 are local constants. Relocation and the global fixed point produce the same **8-byte** binary and one executed instruction before halt; `foo`, `main`, and the multiplication helper are absent.
- `examples/arena_allocator.c` is **1,467 bytes**, returns **1**, and executes 660 instructions. It implements bytewise set/copy operations and an aligned arena allocator in the supported C subset.
- Golden-byte and supplied-ISA encoding comparisons: passed as part of the test suite.

Example binaries and their JSON maps are in `examples/`. The source is `examples/demo.c`.

Validation is against the reference software emulator and the supplied ISA text. No execution on the physical/simulated Turing Complete circuit was performed. Performance numbers count emulator instruction steps, not circuit cycles.
