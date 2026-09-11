# Optimization roadmap

This tracks the standard optimization work proposed for Dynphony C. Checked
items are implemented in version 0.12.0. Partially checked sections describe the
working subset and the remaining work explicitly.

## Current priorities

Completed foundations:

- [x] Fuse comparisons directly into conditional branches.
- [x] Inline small leaf functions, including intrinsic-only helpers such as `move_one`.
- [x] Use immediate static calls for already-laid-out low fixed addresses.
- [x] Keep straight-line intrinsic functions in incoming/caller-saved registers.
- [x] Allocate values that die at calls in `r3`-`r6` without callee-save traffic.
- [x] Perform ABI argument placement as a parallel assignment, breaking register cycles safely.
- [x] Eliminate safe tail calls after restoring the current frame.
- [x] Emit known-symbol calls directly from lowering and retain a separate indirect-call form.
- [x] Emit conditional branches with one explicit target and one CFG fallthrough edge.

The next milestone is one global fixed point containing only Tier 1 transformations:
surviving function bodies are never duplicated, and a transformation is kept
only when it preserves behavior without increasing final code size or runtime.
Every iteration includes both local/CFG and call-graph work; these are not two
one-shot phases.

Next work, in priority order:

1. [x] Introduce explicit basic blocks with predecessor/successor edges and
   compute block reachability from each function entry.
2. [x] Represent startup as an ordinary root IR function containing target startup
   operations, the call to `main`, and termination. The backend only lowers those
   operations; it must not contain a separate `main`-fusion optimization.
3. [x] In each global iteration, run constant/copy propagation, folding, branch
   pruning, dead-code and dead-store removal, and CFG cleanup; then recompute the
   call graph and remove unreachable functions. Conservatively retain functions
   whose addresses remain observable.
4. [x] Relocate/in-line non-recursive functions with exactly one surviving direct
   call site, deleting the standalone body so the transformation duplicates no code.
5. [ ] Relocate a recursive function with exactly one external caller when this
   can be represented without body duplication. Keep a stable label for recursive
   calls and preserve the distinct outer and recursive return continuations.
6. [x] Repeat the entire optimization cycle until no local instruction, CFG edge,
   reachable-function set, or call site changes. Startup's sole call to `main`
   then lets the ordinary one-caller rule absorb `main` and remove call/return overhead.
7. [ ] Complete the no-growth algebraic identity set. Jump threading, adjacent
   trivial-block merging, and unreachable-block removal already run in the fixed point.
8. [x] Relax forward static calls and branches after final layout without retaining
   unreachable padding.
9. [ ] Track flag liveness across instructions known to preserve flags.
10. [ ] Add bounded compile-time evaluation of side-effect-free calls with known
    arguments after the Tier 1 fixed point is stable. This can fold examples such
    as `factorial(5) + 22` without recursively duplicating code.
11. [ ] Add SSA construction and sparse conditional constant propagation, followed
    by block liveness, register reuse, local value numbering, and loop-invariant
    code motion where each individual transform satisfies the Tier 1 policy.

## Deferred and optional work

- [ ] Add an opt-in stack/heap collision guard, disabled by default so normal
  freestanding output retains its current cost and behavior. The guard should
  check a proposed dynamic stack allocation before changing `sp`, reject address
  wraparound, and jump to a named infinite `_stack_overflow` halt loop on VLA
  exhaustion. A future heap runtime should use the same boundary from the
  opposite direction and return `NULL` from `malloc` on collision. Decide the
  public target option and failure-hook policy before implementation; do not
  silently add checks to ordinary release output.
- [ ] Defer function-pointer devirtualization. Continue using conservative
  address-taken reachability for now.
- [ ] Defer PIC startup self-relocation. If implemented later, place it behind an
  explicit non-default option and benchmark it against the current `r13`
  base-relative sequences. Dynphony branches are absolute, not PC-relative, so
  code branch sites are candidates too. Patch sites must retain fixed width;
  account for the relocation table, patching code, startup time, program reentry,
  and writable-code assumptions.
- [ ] Consider specialization, multi-site inlining, and loop unrolling only behind
  an explicit speed/size policy because they can grow the image.
- [ ] Treat self-hosting as a separate language/runtime roadmap. Structs, an arena,
  and bootstrap stages are useful project goals, but they are not optimizer passes.

## 1. Control-flow graph

- [x] Construct explicit basic blocks from each function's linear serialized IR.
- [x] Record predecessor and successor edges.
- [x] Remove instructions after unconditional transfers through the next block boundary.
- [x] Remove unreachable blocks using graph reachability.
- [x] When a condition folds, remove its untaken CFG edge and delete the adjacent
  fallthrough block if it has no other reachable predecessor.
- [x] Thread jumps through blocks containing only labels and an unconditional jump.
- [x] Merge adjacent blocks when removing a jump to any immediately following label.
- [x] Choose the lexical fallthrough edge in IR and invert the condition when the
  true block follows, before rebuilding the CFG in the global fixed point.
- [ ] Reorder non-adjacent blocks using execution frequency and size costs.

## 2. Constant propagation and folding

- [x] Fold integer arithmetic and bitwise operations with constant operands.
- [x] Fold constant comparisons, shifts, unary operations, and casts.
- [x] Fold constant branches.
- [x] Remove arithmetic helpers when folding eliminates their last operation.
- [x] Propagate constants conservatively within basic blocks.
- [ ] Propagate constants across blocks using data-flow information.

## 3. Copy propagation and algebraic simplification

- [x] Propagate safe copies within basic blocks.
- [x] Preserve snapshot semantics when a copied mutable value is later reassigned.
- [x] Remove redundant same-width casts.
- [x] Simplify multiplication by zero, one, and powers of two.
- [x] Simplify unsigned division and remainder by powers of two.
- [x] Add the core identity set, including `x + 0`, `x - 0`, `x & 0`,
  `x | 0`, `x ^ x`, and shifts by zero.
- [ ] Combine longer cast and copy chains across blocks.

## 4. Promote locals to IR values

- [x] Promote scalar locals whose addresses do not escape.
- [x] Keep arrays, aliased objects, and address-escaping locals in memory.
- [x] Preserve assignments and loop-carried values with mutable virtual values.
- [ ] Convert promoted locals to SSA form.
- [ ] Insert phi nodes at control-flow joins.

## 5. Sparse conditional constant propagation

- [ ] Implement SSA-based sparse conditional constant propagation.
- [ ] Propagate constants through phi nodes.
- [ ] Discover executable edges while propagating values.
- [ ] Remove blocks and edges proven unreachable.

## 6. Register allocation across control flow

- [x] Keep up to four frequently used values in `r8`-`r11` across branches and loops.
- [x] Preserve allocated values across nested calls.
- [x] Save and restore only the callee-saved registers selected by each function.
- [ ] Compute block-level liveness.
- [ ] Build live intervals or an interference graph.
- [ ] Reuse registers for non-overlapping values.
- [ ] Use spill costs based on loop depth and use frequency.
- [ ] Prefer caller-saved registers for values that do not cross calls.
- [x] Prefer `r3`-`r6` for selected values whose live ranges do not cross calls.
- [x] Treat software multiply/divide/remainder operations as calls for liveness.
- [x] Stage mixed argument sources safely and resolve register-only argument permutations in parallel.

## 7. Common-subexpression elimination

- [ ] Reuse repeated arithmetic and address calculations.
- [ ] Add local value numbering within basic blocks.
- [ ] Add global value numbering after SSA construction.
- [ ] Reuse loads only when conservative alias analysis proves it safe.
- [ ] Invalidate memory expressions across stores, calls, and device intrinsics.

## 8. Loop optimization

- [ ] Identify natural loops and their nesting depth.
- [ ] Move loop-invariant calculations out of loops.
- [ ] Simplify induction variables.
- [ ] Detect constant trip counts.
- [ ] Remove redundant loop loads and stores.
- [ ] Recognize count-down loops when they are cheaper for Dynphony.

## 9. Inlining

- [x] Relocate arbitrary non-recursive single-call-site functions without duplicating their bodies.
- [x] Inline small single-return leaf functions when they have one surviving call site.
- [ ] Inline the `__dyn_udiv` and `__dyn_umod` wrappers when profitable.
- [x] Re-run constant propagation, CFG cleanup, reachability, and dead-code elimination after inlining.
- [ ] Limit recursive and mutually recursive inlining.

## 10. Backend relaxation and peephole optimization

Keep a small symbolic machine-instruction form through register allocation and
layout. Run peephole cleanup there, then perform final relaxation and byte
encoding. The peephole pass handles backend artifacts only; semantic rewrites
belong in IR. Every rewrite must preserve flag behavior as well as register and
memory behavior.

- [x] Use immediate backward jumps when a fixed target fits 16 bits.
- [x] Convert low forward unconditional jumps to one executed immediate jump while preserving layout.
- [x] Remove jumps to the immediately following label.
- [x] Remove an unnecessary branch to the final function epilogue.
- [x] Avoid saving unused callee-saved registers.
- [x] Relax forward branches without retaining padding.
- [x] Use immediate static calls for known backward/already-laid-out targets that fit 16 bits.
- [x] Relax forward calls to immediate form after final layout.
- [x] Lower the branch direction and fallthrough already selected by IR without
  repeating that optimization in the backend.
- [ ] Coalesce redundant register moves.
- [ ] Eliminate redundant reloads after register allocation.
- [ ] Remove identity moves such as `mov r1, r1` when flags and observable state
  are unchanged.
- [ ] Fold adjacent backend-generated moves and address materializations when the
  shorter sequence has identical flag behavior.
- [ ] Remove redundant spill/reload pairs using physical-register and memory
  alias information.
- [ ] Iterate peephole cleanup and branch/call relaxation together. Branch/call
  sizes and label addresses already relax to a fixed point; the machine peephole
  pass is still missing.

## Runtime library and dynamic memory

- [ ] Ship `memcpy`, `memmove`, `memset`, and `memcmp` as ordinary runtime C
  functions and include each one only when referenced.
- [ ] Define the heap between the aligned end of static data and the descending
  stack, using configured RAM size rather than a hard-coded address.
- [ ] Provide `malloc`, `free`, `calloc`, and `realloc` through a compact aligned
  free-list allocator with block splitting and adjacent-block coalescing.
- [ ] Detect allocation failure and heap/stack collision without requiring a
  memory-management construct in the C language.
- [ ] Retain the arena allocator as an optional specialized allocator and example,
  rather than requiring programs to paste it in for ordinary allocation.

## Optional loop unrolling

- [ ] Add an optimization-level and code-size policy before enabling unrolling.
- [ ] Fully unroll very small loops with proven constant trip counts.
- [ ] Optionally partially unroll larger fixed loops by factors such as two or four.
- [ ] Add an explicit `--unroll-loops` option for growth-oriented optimization.
- [ ] Disable growth-oriented unrolling in a future size-optimization mode.
- [ ] Benchmark partial unrolling of the 32-round division loop; keep it rolled by default.

## Recursion and tail calls

- [x] Eliminate direct self-tail calls when the caller has no addressable local object that may escape.
- [x] Eliminate safe sibling tail calls under the same frame-lifetime constraint.
- [x] Perform parallel argument moves without clobbering inputs.
- [ ] Combine tail-call elimination with inlining and control-flow cleanup.

General non-tail recursion is intentionally outside the loop-conversion task: it
requires preserving pending call state, normally through the machine stack or an
equivalent explicit stack.
