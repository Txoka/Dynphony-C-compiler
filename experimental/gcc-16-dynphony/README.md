# GCC 16 Dynphony backend bootstrap

This directory is a staging area for a Dynphony target for GCC 16.x.  It is not
part of `dyncc` and is intentionally laid out so the files can be copied into a
GCC 16.2 source tree while the target is being brought up.

## Architectural model

The target deliberately distinguishes hardware facts from ABI choices.

Hardware facts used by the backend:

- 16 32-bit encoded registers.
- `r0`/`zr` is the only structurally special register and always reads as zero.
- `r1..r15` are otherwise ordinary storage registers.
- integer and pointer width is 32 bits; bytes are 8 bits; memory is big-endian.
- ALU operations are three-address and have register and unsigned-16-immediate forms.
- loads/stores are 8/16/32 bit through either a register address or an unsigned-16 absolute address.
- comparison/conditional-branch encodings transiently use encoded register 15.  The GCC port models this as a clobber of r15, so r15 remains allocatable when its value does not cross a comparison.
- `push`, `pop`, `call`, `ret`, `sp`, and `flags` are assembler/convention constructs, not evidence that r14/r15 are intrinsically dedicated hardware registers.

Initial GCC ABI choices, made for compatibility with the current Dynphony C compiler:

- r1-r6: integer/pointer arguments.
- r1: scalar return value.
- r1-r7 and r15: caller-saved.
- r8-r13: callee-saved.
- r14: GCC stack pointer for this ABI version.
- r12: initial hard frame pointer.

The r14/r12 assignments are *backend policy*.  They are isolated so that later
ABI experiments can benchmark other choices.  In particular, r15 is not globally
reserved merely because the existing assembler calls it `flags`.

## Files

Copy `gcc/config/dynphony/` to the same location in a GCC 16.x source checkout.
`config.gcc.fragment` shows the stanza that must be merged into GCC's top-level
`gcc/config.gcc`.

This bootstrap is intended to reach, in order:

1. `dynphony-unknown-elf-gcc -S` for freestanding integer C.
2. Correct prologue/epilogue, calls, branches, byte/halfword accesses, and large constants.
3. A Dynphony assembler/linker path so `gcc` can produce runnable flat images rather than only assembly.
4. Differential execution against the existing emulator and `dyncc` tests.
5. Cost tuning, peepholes, libgcc multiply/divide helpers, tail calls and frame-pointer elimination.
6. Experiments with interprocedural/internal calling-convention specialization outside the fixed public ABI.

## Important bring-up caveat

GCC is only one component of the normal `gcc -> as -> ld` pipeline.  Dynphony has
no GNU binutils target yet.  These files therefore start with the compiler target;
producing final flat Dynphony binaries still needs either a small Dynphony assembler/linker
or a binutils port.  The existing Python assembler in this repository is an excellent
oracle for that stage.

Target baseline: GCC 16.2 (released 2026-08-07).
