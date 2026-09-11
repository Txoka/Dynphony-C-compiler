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
- [x] Model promoted locals with transient SSA versions and phi joins during sparse analysis.
- [x] Run sparse conditional constant propagation across blocks and remove infeasible CFG edges.
- [x] Propagate immutable copy/cast chains across blocks without breaking mutable snapshots.
- [x] Legalize surviving software arithmetic into explicit Dynphony runtime calls, then rerun
  the global fixed point so ordinary reachability and inlining remove one-use wrappers.
- [x] Inline callees containing tail calls by preserving their outer continuation.
- [x] Convert safe direct self-tail recursion into parallel parameter updates and a loop backedge.
- [x] Apply the expanded no-growth identity set, including self-comparisons and division or
  remainder by one.
- [x] Remove unreachable globals together with dead initializer-relocation chains and
  functions referenced only by dead function-pointer data.
- [x] Fold typed big-endian loads from proven immutable scalar and array globals, including
  zero-addend symbolic pointer initializers, and rerun the global fixed point.

The next milestone is one global fixed point containing only Tier 1 transformations:
surviving function bodies are never duplicated, and a transformation is kept
only when it preserves behavior without increasing final code size or runtime.
Every iteration includes both local/CFG and call-graph work; these are not two
one-shot phases.

Remaining work, in dependency and payoff order:

1. [ ] Add a bounded IR evaluator for side-effect-free calls and loops with known
   inputs. Give it explicit instruction, recursion-depth, and memory limits; reject
   device operations, volatile access, unknown calls, undefined operations, and
   writes outside private evaluator state. Model immutable global bytes and private
   local storage in the evaluator, then feed successful results back into the
   ordinary fixed point. Together with the completed global passes, this should collapse the
   constant demo to `mov r1, 146` plus halt.
2. [ ] Identify natural loops, induction variables, and proven constant trip counts.
   Use these facts for loop-invariant code motion and bounded evaluation first;
   retain code-growing unrolling behind its explicit option.
3. [ ] Recognize matching quotient/remainder expressions with identical proven-pure
   operands and lower them to a two-result `divmod` IR operation. Preserve signed
   C semantics and reject intervening mutation, volatile access, and calls.
4. [ ] Add local value numbering, then global value numbering, using conservative
   alias invalidation for loads. This removes repeated arithmetic and address work.
5. [ ] Add block liveness and interference-based register/stack-slot reuse, followed
   by loop-depth spill costs and better caller-saved allocation.
6. [ ] Add the small post-allocation peephole pass and iterate it with branch/call
   relaxation. It should only remove artifacts requiring physical-register knowledge.
7. [ ] Add non-tail recursive fallthrough-call layout for a recursive function
   with exactly one external direct caller. Split the caller around that site,
   pre-push its known continuation, place the recursive function next, and let
   the first entry fall through while recursive entries keep calling the stable
   function label. Compare the ordinary-call and fallthrough layouts after all
   alignment and branch/call relaxation; apply it only when binary size does not
   increase and first-entry runtime cost decreases or remains equal. Reject it
   when the function address is observable, the call is indirect, code movement
   lengthens another executed path, PIC continuation materialization erases the
   saving, or stack-argument/frame cleanup cannot be preserved exactly. Add
   regression tests for fixed and PIC images, low and high addresses, stack-passed
   arguments, nested non-tail recursion, caller continuation execution, `sp`
   restoration, stable recursive entry labels, branch-distance thresholds, and
   a deliberately unprofitable layout that must remain unchanged.
8. [ ] Add flag liveness and profile/cost-guided block ordering after the preceding
    CFG and register foundations are stable.
9. [ ] Extend singleton function-pointer recognition through immutable global loads.
   Local single-target pointers already reduce to direct calls after copy propagation.
   Keep guarded multi-target devirtualization deferred because it can grow code.
10. [x] Implement the memory runtime, heap allocation, overflow-safe VLA sizing,
    and bidirectional heap/stack collision checks independently of optimizer correctness.

## Deferred and optional work

- [x] Guard dynamic stack allocation against zero/overflowed sizes, address
  wraparound, static data, and the live heap boundary. Invalid VLA allocation
  enters the named `_stack_overflow` loop; heap growth returns `NULL` when it
  reaches the live stack.
- [ ] Defer guarded multi-target function-pointer devirtualization. Continue using
  conservative address-taken reachability when a singleton target cannot be proven.
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
- [x] Propagate constants across blocks using data-flow information.

## 3. Copy propagation and algebraic simplification

- [x] Propagate safe copies within basic blocks.
- [x] Preserve snapshot semantics when a copied mutable value is later reassigned.
- [x] Remove redundant same-width casts.
- [x] Simplify multiplication by zero, one, and powers of two.
- [x] Simplify unsigned division and remainder by powers of two.
- [x] Add the core identity set, including `x + 0`, `x - 0`, `x & 0`,
  `x | 0`, `x ^ x`, and shifts by zero.
- [x] Combine immutable longer cast and copy chains across blocks while preserving
  snapshots of mutable promoted locals.

## 4. Promote locals to IR values

- [x] Promote scalar locals whose addresses do not escape.
- [x] Keep arrays, aliased objects, and address-escaping locals in memory.
- [x] Preserve assignments and loop-carried values with mutable virtual values.
- [x] Construct transient SSA versions for promoted locals during sparse analysis.
- [x] Model phi values at control-flow joins and lower the results back to ordinary
  IR before code generation.

## 5. Sparse conditional constant propagation

- [x] Implement SSA-based sparse conditional constant propagation.
- [x] Propagate constants through transient phi joins.
- [x] Discover executable edges while propagating values.
- [x] Remove blocks and edges proven unreachable.

## Whole-program globals and compile-time evaluation

- [x] Remove unreferenced globals from the module before binary layout.
- [x] Follow global-initializer relocations transitively, so a live pointer keeps
  its target alive while an unreachable pointer and target can both disappear.
- [x] Recompute PIC startup relocation work after global deletion.
- [x] Prove immutable globals conservatively using direct stores and address escapes.
- [x] Fold typed big-endian scalar and array loads from proven-immutable initialized data.
- [ ] Fold symbolic pointer initializers with nonzero addends; zero-addend pointers
  already become ordinary `global_addr` values and compound through the fixed point.
- [ ] Refine interprocedural alias summaries so passing an address to a known,
  non-writing callee does not unnecessarily disqualify immutable data.
- [ ] Evaluate bounded side-effect-free loops and calls with known arguments.
- [ ] Re-run SCCP, call-graph reachability, and dead-global elimination after each
  successful compile-time evaluation.

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
- [x] Legalize surviving software arithmetic to explicit runtime calls and inline
  the `__dyn_udiv` and `__dyn_umod` wrappers when they have one surviving use.
- [x] Re-run constant propagation, CFG cleanup, reachability, and dead-code elimination after inlining.
- [x] Convert tail calls inside relocated callees back into calls to the outer
  continuation, then rerun tail-call and CFG cleanup in the global fixed point.
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

- [x] Ship `memcpy`, `memmove`, `memset`, and `memcmp` as ordinary runtime C
  functions and include each one only when referenced.
- [x] Define the heap between the aligned end of static data and the descending
  stack, using configured RAM size rather than a hard-coded address.
- [x] Provide `malloc`, `free`, `calloc`, and `realloc` through a compact aligned
  free-list allocator with block splitting and adjacent-block coalescing.
- [x] Detect allocation failure and heap/stack collision without requiring a
  memory-management construct in the C language.
- [x] Retain the arena allocator as an optional specialized allocator and example,
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
- [x] Snapshot tail-call arguments in parallel, rewrite safe self-tail recursion
  as a CFG backedge, and let single-caller relocation absorb the resulting loop.
- [x] Eliminate safe sibling tail calls under the same frame-lifetime constraint.
- [x] Perform parallel argument moves without clobbering inputs.
- [x] Combine tail-call elimination with inlining and control-flow cleanup in
  every global fixed-point iteration.

General non-tail recursion is intentionally outside the loop-conversion task: it
requires preserving pending call state, normally through the machine stack or an
equivalent explicit stack.
