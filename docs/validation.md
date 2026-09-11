# Validation record

Validated in the development environment on 2026-09-11 with Python 3.13 and pycparser 2.22.

- `python -m unittest discover -s tests -v`: **70 test methods passed**, including package boundaries, preprocessing, multi-translation-unit and internal linkage, multidimensional VLA strides and runtime `sizeof`, VLA overflow/collision trapping, named registers and ABI roles, canonical lowering, `_Bool`/`bool` conversions, literal-format `printf`, structures, enums, `const`, register and stack-passed arguments, the free-list allocator and memory functions, optimization passes, device intrinsics, and fixed/PIC termination.
- Native C emulator build (`make native`) and differential execution against the
  Python reference: passed on x86-64 Linux with CPython 3.13. Native execution of
  the `pi.c` workload measured approximately **239 MHz**, versus approximately
  **0.4 MHz** for the Python interpreter in this development environment.
- Wheel build with `pip wheel --no-build-isolation --no-deps`: passed.
- Wheel installed into a separate directory; imported outside the source checkout and compiled/executed `6 * 7`: returned **42**.
- Fixed-address demonstration: **662 bytes**, returned **146**, 945 emulated instructions. Before the optimization passes it was 1,572 bytes and 2,195 instructions.
- PIC demonstration: **848 bytes**, loaded at `0x12345`, returned **146**, 1,196 emulated instructions. Before the optimization passes it was 1,708 bytes and 2,319 instructions.
- Towers of Hanoi demonstration: **260 bytes**, returned **0**, and emitted the expected 28 device outputs in **272 emulated instructions** for inputs `2, 0, 2, 1`. Its second recursive call is now a loop backedge; the first remains a non-tail recursive call with a distinct frame. Before post-layout control relaxation and fallthrough selection it was 348 bytes and 370 instructions; the original compiler output was 779 bytes and 878 instructions.
- The focused constant-local example `int a=12345; int b=6789; return a+b;` is **8 bytes** and executes one instruction before reaching halt. Generic single-caller relocation absorbs `main`; the image contains one immediate constant instruction and one halt jump.
- `examples/constant_folding.c`, containing `return a + b * c` for constants 4, 34, and 43, is also **8 bytes**. It executes `mov r1, 1466` before halt and includes no multiplication helper.
- `examples/interprocedural_constant_folding.c` passes 4 from `main` into the sole-called `foo`, where 34 and 43 are local constants. Relocation and the global fixed point produce the same **8-byte** binary and one executed instruction before halt; `foo`, `main`, and the multiplication helper are absent.
- `examples/arena_allocator.c` is **1,194 bytes**, returns **1**, and executes 596 instructions. It implements bytewise set/copy operations and an aligned arena allocator in the supported C subset.
- Golden-byte and supplied-ISA encoding comparisons: passed as part of the test suite.

Example binaries and their JSON maps are in `examples/`. The source is `examples/demo.c`.

Validation is against the reference software emulator and the supplied ISA text. No execution on the physical/simulated Turing Complete circuit was performed. Performance numbers count emulator instruction steps, not circuit cycles.
