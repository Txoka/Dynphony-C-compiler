# Pipeline and extension guide

## Boundaries

`Compiler` orchestrates a `SourceFrontend` and a target backend. `CFrontend`
owns every C-specific preparation step, including parser invocation, injected
device declarations and arithmetic runtime source, semantic analysis, and
lowering. A future frontend can implement the same source-to-`ModuleIR` protocol
without importing pycparser or changing middle-end and Dynphony target modules.

Each translation unit first passes through the built-in token-aware preprocessor,
then `parse` uses pycparser's lexer/parser. Translation units are type-checked in
independent scopes and linked at the typed-program boundary; internal symbols are
namespaced before the shared whole-program optimizer runs. Runtime helpers are
parsed separately so diagnostics retain the user's filename and line numbers.

`Frontend.build` collects function/global symbols, structure and enum tags, then checks global initializers and function bodies. Its result consists solely of project-owned `Type`, `Record`, `Symbol`, `Node`, `Global`, and `Function` records. Structure records are shared objects so an incomplete `struct Node` can be referenced through a pointer while its members are being completed. Member offsets, natural alignment, const qualification, implicit integer conversions, and array/function decay are explicit before IR lowering. Unsupported constructs fail here instead of reaching machine emission.

`lower` converts the typed tree to a function-local list of canonical instructions. Values are numbered, local/global addresses are explicit, and loads/stores have widths. Calls through named function symbols are emitted directly as `direct_call`, while genuine function-pointer calls use the indirect `call` form. Structured control flow is emitted as a one-target `branch_if` whose other CFG edge is lexical fallthrough; comparison fusion later produces `cbranch_if` in the same form. Short-circuit and conditional expressions introduce branches and a shared result slot. Address computation for compound assignment and increment happens exactly once. The backend receives no parser AST.

`lower` also creates `_start` as an ordinary root IR function containing target initialization, a direct call to `main`, and termination. `optimize` repeatedly combines local simplification, explicit CFG reachability, call-graph reachability, and no-duplication single-caller relocation until the whole module stops changing. It can turn an indirect call into a direct call when propagation later proves its target, promotes read-only parameters and non-escaping scalar locals, folds scalar operations and branches, fuses comparison/branch pairs, eliminates safe tail calls, applies algebraic and power-of-two reductions, and removes unused values, blocks, functions, and arithmetic helpers.

The Dynphony target separates architectural register names, ABI roles, target/image configuration, ISA encoding, symbolic assembly/relaxation, and backend instruction selection. `Backend` assigns slots to objects and live computed values, rematerializes constants and addresses at their uses, selects immediate instructions, and emits symbolic fixups through `Assembler`. IR has already chosen direct calls, branch direction, and fallthrough; the backend does not rediscover those semantic relationships. Static data is appended with alignment and explicit zero bytes. For fixed-address images, `Assembler.finish` repeatedly relaxes symbolic branches and direct calls after layout, updates label and relocation offsets, and emits the shortest legal target form without padding. That target-width decision requires final byte addresses and remains machine-specific. Data relocations are finalized afterwards. PIC address constants occupy 12 bytes (three immediate ALU operations) plus a 3-byte base add. Ordinary constants use a four-byte immediate form when possible.

The middle end owns a reusable fixed-point pass manager. The pipeline schedules
local scalar/CFG transformations and module call-graph transformations as explicit
pass groups, comparing canonical IR snapshots until the module stops changing.
New analyses belong under `middle/analysis`; new transformations belong under
`middle/passes`.

## Stack frame and calls

At entry to a generated function, the caller's return address is at `[sp]`. A function that needs a frame pushes the old `r12`, copies `sp` into `r12`, and allocates its aligned local/temporary area below it:

```text
higher addresses
  return address          [r12 + 4]
  caller's frame pointer  [r12]
  local objects           [r12 - local_offset]
  virtual-value slots     [r12 - temp_base - 4*(value_id+1)]
  current sp
lower addresses
```

Constants and addresses are rematerialized. The local allocator keeps values for a straight-line leaf function in caller-saved registers and coalesces promoted parameters with their incoming `r1`–`r6` locations. Functions with control flow assign up to four frequently used values to `r8`–`r11` and save only the registers selected for that function. Other live values are backed by memory, so nested calls and loop backedges remain safe. Parameters that are assigned or whose addresses escape remain addressable stack objects. Arguments seven onward are pushed right-to-left; after the callee saves its frame pointer and selected callee-saved registers, it loads those arguments from positive frame-pointer offsets. The caller discards them after return. Indirect call targets are preserved before argument placement. The register call sequence is exactly the supplied pseudo-instruction: `counter flags`, add 16, push flags, and jump to the target register. A frame-using function restores `sp` and `r12` before the ISA's `ret` expansion. A frame-free function goes directly to `ret`.

The backend uses `r1` and `r2` for arithmetic and `r7` for addresses. Allocated values may reside in `r8`–`r11`; `r12` is the frame pointer and `r13` holds the PIC base. Every modified callee-saved register is preserved. Materializing a jump address never modifies flags, so nothing between a comparison and its consuming conditional jump invalidates that comparison.

Values proven not to cross a call may also reside in `r3`–`r6`. ABI argument
placement is treated as a parallel register assignment: register-only cycles use
`r7`, while mixed computed and register sources conservatively stage through the
stack. Static calls use the immediate ISA form when their fixed target is already
known and fits 16 bits. Tail calls restore the current frame before jumping and
are suppressed when an addressable local could be passed into the callee.

## Integer and pointer semantics

Narrow loads zero-extend in hardware. The backend uses left shift followed by arithmetic right shift to sign-extend signed narrow types. The same conversion sequence handles narrowing assignment/casts. Narrow integer operands promote to signed 32-bit int; unsigned 32-bit operands drive unsigned common arithmetic. Right shifts use the promoted left operand's signedness. Pointer arithmetic scales by the pointed-to object size, and pointer difference divides the byte difference by that size.

The initial implementation models `long` and `int` using the same 32-bit representation and conversion behavior. Structures use natural member alignment capped at four bytes and retain tail padding. Enums currently use signed 32-bit `int`. `const` is represented on types and prevents writes or qualifier-discarding pointer conversions; `volatile` and `restrict` remain unsupported. A future standards-focused frontend should preserve integer rank explicitly before adding wider types.

Static constant evaluation operates on the typed AST and applies width/sign normalization at each conversion and arithmetic operation. Static addresses remain `(symbol, byte_addend)` records until layout. PIC initialization in `_start` emits address stores before the optimized program body, deriving both destination and target from `r13`. No relocation metadata is required in the raw binary.

## Software arithmetic

Multiplication shifts the multiplier right and adds selected shifted multiplicands. Unsigned division uses 32 rounds of restoring division; retaining the carry from the partial remainder avoids losing the 33rd bit. Signed wrappers convert operand bit patterns to unsigned magnitudes and restore the quotient/remainder signs. After target-independent folding and strength reduction, Dynphony legalization turns surviving software arithmetic into explicit runtime calls and reruns the global optimizer, allowing ordinary call-graph cleanup and wrapper relocation. There is no hardware multiplication, division, or host-side execution shortcut in generated images.

## Extending the compiler

- Add syntax/semantics in `frontend.py`, representing conversions and lvalues explicitly.
- Add a typed AST operation only when existing operations cannot express the semantics cleanly.
- Lower new operations in `ir.py`; keep expression-tree decisions out of the backend.
- Add machine selection in `backend.py` and primitive encodings in `isa.py`.
- Add execution tests for observable behavior and encoding checks against the ISA.
- Extend `optimizer/pipeline.py` with SSA phi nodes, inter-block propagation, common-subexpression elimination, and loop analysis. Reusable graph analysis belongs in `optimizer/` modules such as `cfg.py`.
- Extend allocation with CFG liveness, interval reuse, and spill-cost estimates while preserving call liveness and callee-saved register rules.
- For wider integers, add explicit multiword IR and helper conventions before changing frontend literal/type rules.
- Device calls are typed as built-in C declarations, lowered to explicit intrinsic
  IR operations, and selected directly as ISA instructions. This keeps device
  semantics out of the parser and avoids ABI call overhead.

## Validation and practical limits

The unittest suite groups many execution cases and deterministic randomized arithmetic cases. It covers:

- ISA encodings checked against both explicit golden bytes and the attached text.
- Integer promotion, narrowing, sign extension, mixed signedness and comparisons.
- Both branches of short-circuit and conditional evaluation, including side effects.
- Loop control, nested scopes, recursion, nested calls, register and stack-passed scalar arguments, and callee-saved state.
- Array/structure initialization, padded and self-referential structures, member access, enums, const pointers, local typedefs, static locals, multidimensional indexing, pointer chains, scaled pointer differences and indirect calls.
- Big-endian byte inspection, unaligned word access and modulo RAM access.
- Fixed addresses beyond 64 KiB, far static data, PIC at multiple aligned/unaligned addresses, pointer relocations and PIC reentry.
- Software arithmetic with boundary operands, negative quotient/remainder cases, and seeded randomized operands.
- Rejection diagnostics, target sizing and a full CLI compile/map/IR/emulator workflow.

The emulator is independently decoded from instruction bytes rather than interpreting the compiler's IR. It is still a software reference model, so encoding fixtures and real-circuit verification remain valuable. It models the instructions generated by this compiler, including I/O, time, and persistent storage. The supplied spec does not give numeric flag bit assignments, so the emulator tracks comparison relations internally. Hardware shift behavior for counts outside C's valid range is not relied upon.
