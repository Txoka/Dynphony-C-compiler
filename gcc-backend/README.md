# GCC dynphony backend (bootstrap, for comparison only)

A minimal GCC machine-description target for the dynphony/Symphony machine
(`dynphony.cc`, `dynphony.h`, `dynphony.md`, `dynphony.opt`,
`dynphony-protos.h`), built to compile the same example C programs with real
GCC (`-Os`/`-O2`) as a benchmark baseline for the dyncc optimizer roadmap
(see the `project-dyncc-optimizer-roadmap` memory).

This is not wired into dyncc's own build — it's a standalone GCC target
description meant to be dropped into a GCC source tree's `gcc/config/dynphony/`
and built as a cross-compiler, purely so dyncc's codegen/optimizer output can
be measured against GCC's on the same source files.

There is no real `dynphony`/Symphony target upstream in GCC, so this backend
is bootstrapped by **hijacking GCC's existing `moxie` target**: moxie is a
simple, libc-free, mnemonic-compatible toy architecture already built into
GCC, so `dynphony.md`/`dynphony.cc` piggyback on moxie's instruction
selection and just describe it in terms close enough to Symphony's ISA that
the resulting `.s` assembly can be hand-assembled into real Symphony machine
code. This is why the build step below configures GCC `--target=moxie-elf`
rather than a `dynphony-elf` triple.

## Building the cross-compiler

```sh
# from a GCC source checkout (tested with a recent gcc release branch)
mkdir build-moxie && cd build-moxie
../configure --target=moxie-elf --prefix=/opt/moxie-gcc \
    --disable-nls --without-headers --enable-languages=c
make -j"$(nproc)" all-gcc
make install-gcc
```

No libc is built or needed — every example program compiled for comparison
must avoid libc calls (or stub them identically on both sides, see below).

## Compiling an example for comparison

```sh
/opt/moxie-gcc/bin/moxie-elf-gcc -S -Os examples/insertion_sort.c -o insertion_sort.Os.s
/opt/moxie-gcc/bin/moxie-elf-gcc -S -O2 examples/insertion_sort.c -o insertion_sort.O2.s
```

This produces moxie assembly using Symphony-compatible mnemonics
(`mov`, `add`/`addi`, `cmp`, conditional jumps, `call`/`ret`, `load32`/
`store32`, …). It is **not** yet real Symphony machine code — moxie's
encoding and instruction widths differ from Symphony's fixed 4-byte
instruction slots.

## Assembling to real Symphony machine code

`tools/gcc_assembler.py` is a minimal two-pass assembler that turns this
`.s` output into actual Symphony-encoded bytes runnable on dyncc's own
emulator, by re-encoding each moxie mnemonic through
`symphony.targets.symphony.isa`:

```sh
python3 gcc-backend/tools/gcc_assembler.py insertion_sort.Os.s
```

It prints the assembled size and resolved label addresses. Import
`assemble()` directly to get raw bytes for feeding into
`symphony.emulator` alongside dyncc's own compiled output, so both sides run
through the identical emulator.

Three real correctness bugs had to be fixed in this assembler before its
output was trustworthy (all fixed in the current version — noted here since
they were easy to get subtly wrong again if this is ever rewritten):

1. **Fixed instruction padding.** Symphony pads every raw instruction to 4
   bytes; the assembler must mirror `Assembler.emit` in
   `symphony/targets/symphony/assembler.py` (`pad_fixed_width`) or addresses
   computed for jumps/calls land on the wrong byte offsets.
2. **Local label collisions.** GCC's local labels (`.Lnn` control-flow
   labels, `.LCnn` constant-pool labels) are only unique *within one
   compilation unit*. Concatenating multiple `.s` files (e.g. a hand-written
   runtime trampoline plus a compiled program) can silently collide two
   same-numbered labels from different functions and corrupt control flow
   with no assembler error. `namespace_local_labels(text, prefix)` rewrites
   every `.L...` label per input file before concatenating.
3. **Wrong `call` return offset.** `isa.call()`'s default `return_offset`
   (16) assumes Dynphony's variable-width encoding. Symphony pads every
   sub-instruction of a call sequence to 4 bytes, so the real byte distance
   from the return-address computation to the instruction after the call is
   20, not 16 — pass `return_offset=20` explicitly (already done in
   `encode_insn`'s `call` case).

## Running a fair comparison

Both GCC's moxie output and dyncc's own compiled output need to run through
the **same** small I/O trampoline (`_start`/`input`/`output`) and the same
software helpers for anything moxie/Symphony lacks in hardware (multiply,
`memcpy`, etc.) — write these once by hand and link/concatenate them with
both sides identically.

The comparison is only meaningful if every stubbed intrinsic does
*equivalent* work on both sides. moxie has no libc, so calls like `printf`
must either be avoided in the compared source, or stubbed with real
(not no-op) work on both sides — a no-op stub on GCC's side while dyncc's
compiled binary does real formatting work produces a large, fake gap that
has nothing to do with either optimizer. See the `project-dyncc-optimizer-roadmap`
memory's "how to measure fairly" note for a worked example (the `primes.c`
`printf` case, which first looked like a ~100x gap and was actually a ~3x
one once `printf` was removed from both sides).

Compare using cycle counts (emulator step count from `_start` to halt) and
final binary size, not wall-clock time — wall-clock time on a Python
reference emulator measures the emulator, not the compiled code.

## Status

This backend and its assembler are hand-verified against a handful of
example programs (see `project-dyncc-optimizer-roadmap` memory for current
numbers) but do not yet have automated tests. Treat any comparison numbers
as a manual, reproducible-by-hand benchmark rather than a CI-checked
guarantee until that lands.
