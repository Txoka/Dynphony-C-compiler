# Dynphony GCC backend bring-up plan

The checked-in target is a bootstrap scaffold, not yet a claim of a buildable or
correct GCC port.  It exists so each target decision can be reviewed against the
real Dynphony ISA and `dyncc` before being upstreamed into a GCC checkout.

## Validation gates

### Gate 1: GCC generator/build correctness

Use an unmodified GCC 16.2 release tree, copy `gcc/config/dynphony/`, merge the
`config.gcc` fragment, then configure a C-only cross compiler:

```sh
../gcc-16.2.0/configure \
  --target=dynphony-unknown-elf \
  --enable-languages=c \
  --without-headers \
  --disable-shared \
  --disable-threads \
  --disable-libssp \
  --disable-libquadmath \
  --disable-libatomic
make -j$(nproc) all-gcc
```

Fix all genconditions/genrecog/build errors before judging generated assembly.

### Gate 2: scalar code generation

Compile with `-S -O0`, then `-S -O2`:

- return constants, including 0, U16, negative-small, and arbitrary 32-bit values;
- add/sub/and/or/xor and all shifts;
- signed/unsigned comparisons and every conditional branch;
- byte/halfword/word load/store;
- pointer arithmetic and arrays;
- direct and indirect calls;
- 0..10 integer arguments;
- recursion;
- local frames and spills.

### Gate 3: binary assembly

GCC emits symbolic assembly.  Add a small assembler/linker adapter or binutils
port which supports labels, sections, symbols and relocations and reuses the
canonical instruction encodings from `dynphony/targets/dynphony/isa.py` as the
oracle.  Direct branch/call relaxation should preserve the current dyncc policy:
U16 direct targets when possible, otherwise materialize a 32-bit address and use
the register form.

### Gate 4: differential execution

For each freestanding C test:

1. compile with `dyncc` and GCC;
2. run both flat images in the same Dynphony emulator;
3. compare return value/output/memory-visible behavior;
4. record binary bytes, guest instruction count and maximum stack use.

Correctness is mandatory; size and instruction count are optimization metrics.

### Gate 5: ABI and register-allocation tuning

The initial public ABI mirrors dyncc.  After parity, benchmark alternatives rather
than assuming r14/r12 are intrinsically special.  r0 is the only hardware-fixed
register.  Candidate experiments include frame-pointer elimination, changing the
fixed stack register, changing caller/callee-save split, using r15 for ordinary
values outside compare windows, and eventually specializing conventions for
non-escaping whole-program functions.

A normal GCC backend still has one `STACK_POINTER_REGNUM` per target ABI.  Truly
per-function/per-edge adaptive stack or argument-register conventions would need
additional GCC IPA/LTO work rather than only machine-description tuning.

## Known bootstrap TODOs

- Verify GCC 16 target-hook signatures and generated-header requirements by
  actually building against GCC 16.2.
- Finish prologue/epilogue callee-save emission.
- Correctly model incoming stack arguments and outgoing argument area.
- Add arbitrary 32-bit constant expansion and symbolic address materialization.
- Add memory alternatives for loads, not only stores.
- Add sign/zero extension patterns for QI/HI.
- Add neg/not patterns and useful combine/peephole patterns.
- Implement direct symbolic call patterns and long call/branch relaxation.
- Implement multiply/divide/modulo through libgcc or tuned Dynphony sequences.
- Decide C type model (`char`, `short`, `int`, `long`, `long long`) explicitly.
- Add assembler/linker or GNU binutils support.
- Add GCC-target regression tests and emulator differential tests.
- Tune instruction costs from emulator/hardware cycle data.

Do not merge this target into a GCC source tree and call it production-ready until
all five gates pass.
