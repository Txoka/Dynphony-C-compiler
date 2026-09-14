"""Target-independent scalar, control-flow, and reachability optimization."""

from ..ir import Instruction, ModuleIR
from ...runtime.intrinsics import NAMES as INTRINSIC_NAMES
from ..model import pointer
from ..analysis.cfg import (
    TERMINATORS,
    build_cfg,
    compute_dominators,
    find_natural_loops,
    prune_unreachable_blocks,
)
from .manager import FixedPointPassManager

PURE = {
    "param",
    "const",
    "global_addr",
    "local_addr",
    "cast",
    "copy",
    "load",
    "unary",
    "binary",
}


ADDRESS_PURE = {"local_addr", "global_addr", "binary", "cast", "copy", "const"}


def _address_keys(function, single):
    """Structural value-numbering keys for address-computing values.

    Two values get the same key exactly when they are provably the same
    address without any alias analysis: identical `local_addr`/`global_addr`
    origin, identical literal `const`, or built from the same op/operands
    (recursively keyed the same way). This lets passes recognize repeated
    address expressions (e.g. two lowerings of the same array/struct access,
    each with their own freshly-emitted offset constant) as one location, the
    same way a real value-numbering pass would, without claiming anything
    about memory that isn't a pure address computation.
    """
    keys = {}

    def key(value):
        if value in keys:
            return keys[value]
        instruction = single.get(value)
        if instruction is None or instruction.op not in ADDRESS_PURE:
            result = ("value", value)
        elif instruction.op in ("local_addr", "global_addr"):
            result = (instruction.op, instruction.extra)
        elif instruction.op == "const":
            result = (instruction.op, instruction.extra)
        else:
            result = (instruction.op, instruction.extra, tuple(key(arg) for arg in instruction.args))
        keys[value] = result
        return result

    return key


def _definitions(function):
    result = {}
    for instruction in function.instructions:
        if instruction.dst is not None:
            result.setdefault(instruction.dst, []).append(instruction)
    return result


def promote_scalar_locals(function):
    """Replace non-escaping scalar local memory with mutable virtual values.

    The virtual value may have several ``copy`` definitions. The general backend
    gives such values one spill slot, while straight-line register allocation can
    often eliminate the slot completely. This is a conservative pre-SSA form of
    mem2reg: addressed, aggregate, and aliased objects remain in memory.
    """
    definitions = _definitions(function)
    uses = {}
    for instruction in function.instructions:
        for position, value in enumerate(instruction.args):
            uses.setdefault(value, []).append((instruction, position))

    addresses = {}
    for value, items in definitions.items():
        if len(items) == 1 and items[0].op == "local_addr":
            addresses.setdefault(items[0].extra, []).append(value)

    promotable = {}
    for symbol in function.locals:
        values = addresses.get(symbol.key, ())
        if not values or symbol.type.kind not in ("int", "bool", "pointer"):
            continue
        if all(
            all(
                position == 0 and user.op in ("load", "store")
                for user, position in uses.get(value, ())
            )
            for value in values
        ):
            slot = function.values
            function.values += 1
            for value in values:
                promotable[value] = (slot, symbol.type)

    if not promotable:
        return
    rewritten = []
    for instruction in function.instructions:
        if instruction.op == "local_addr" and instruction.dst in promotable:
            continue
        if instruction.op == "load" and instruction.args[0] in promotable:
            slot, _ = promotable[instruction.args[0]]
            instruction = Instruction(
                "copy", instruction.dst, (slot,), instruction.type
            )
        elif instruction.op == "store" and instruction.args[0] in promotable:
            slot, type_ = promotable[instruction.args[0]]
            instruction = Instruction(
                "copy", slot, (instruction.args[1],), type_
            )
        rewritten.append(instruction)
    function.instructions = rewritten


MEMORY_EFFECTS = {"store", "call", "direct_call", "tailcall", "direct_tailcall", "intrinsic", "stack_alloc"}


def hoist_loop_invariants(function):
    """Move loop-invariant pure computations to a preheader before the header.

    A value is invariant when it has exactly one definition (so it is not a
    mem2reg-promoted mutable slot with several ``copy`` sites), that definition
    is a side-effect-free op, sits inside the loop, and every argument is
    either defined outside the loop or already proven invariant.  Loads are
    only hoisted out of loops with no stores/calls, since the IR carries no
    alias information to prove a load and a store elsewhere never conflict.
    """
    changed = False
    # Loop headers already tried and found unhoistable this call, identified by
    # the header block's first instruction (stable across index-shifting
    # rewrites elsewhere in the function).
    skip_headers = set()
    # Process one loop per CFG snapshot: rewriting instructions can shift block
    # indices, so the CFG, dominators and loop set are rebuilt after each move.
    # Innermost-first (smallest block set) lets a nested loop's invariants
    # become hoistable to its parent once freed of the inner loop's values.
    while True:
        cfg = build_cfg(function)
        if not cfg.blocks:
            break
        dominators = compute_dominators(cfg)
        loops = [
            loop
            for loop in find_natural_loops(cfg, dominators)
            if id(cfg.blocks[loop.header].instructions[0]) not in skip_headers
        ]
        if not loops:
            break

        definitions = _definitions(function)
        single = {value: items[0] for value, items in definitions.items() if len(items) == 1}
        block_of = {}
        for block in cfg.blocks:
            for instruction in block.instructions:
                block_of[id(instruction)] = block.index

        loop = min(loops, key=lambda l: len(l.blocks))
        header_key = id(cfg.blocks[loop.header].instructions[0])
        has_memory_effects = any(
            instruction.op in MEMORY_EFFECTS
            for index in loop.blocks
            for instruction in cfg.blocks[index].instructions
        )
        preheader_predecessors = [
            p for p in cfg.blocks[loop.header].predecessors if p not in loop.blocks
        ]
        if len(preheader_predecessors) != 1:
            skip_headers.add(header_key)
            continue  # no single edge to insert a preheader jump/label pair on
        invariant = set()

        def is_invariant(value):
            if value in invariant:
                return True
            instruction = single.get(value)
            if instruction is None:
                return False
            if block_of.get(id(instruction)) not in loop.blocks:
                return True
            if instruction.op not in PURE or instruction.op == "param":
                return False
            if instruction.op == "load" and has_memory_effects:
                return False
            if not all(is_invariant(arg) for arg in instruction.args):
                return False
            invariant.add(value)
            return True

        hoisted_ids = []
        for index in sorted(loop.blocks):
            for instruction in cfg.blocks[index].instructions:
                if (
                    instruction.dst is not None
                    and instruction.dst in single
                    and single[instruction.dst] is instruction
                    and is_invariant(instruction.dst)
                ):
                    hoisted_ids.append(id(instruction))

        if not hoisted_ids:
            skip_headers.add(header_key)
            continue

        hoisted_ids = set(hoisted_ids)
        preheader = preheader_predecessors[0]
        preamble = [
            instruction
            for instruction in function.instructions
            if id(instruction) in hoisted_ids
        ]
        # Insert before the preheader's terminator (if any) so the preamble
        # stays reachable, rather than after it where it would be dead code.
        last = cfg.blocks[preheader].instructions[-1]
        last_id = id(last)
        insert_before_last = last.op in TERMINATORS
        rewritten = []
        for instruction in function.instructions:
            if id(instruction) in hoisted_ids:
                continue
            if insert_before_last and id(instruction) == last_id:
                rewritten.extend(preamble)
            rewritten.append(instruction)
            if not insert_before_last and id(instruction) == last_id:
                rewritten.extend(preamble)
        function.instructions = rewritten
        changed = True
        # Loop back to the top of the while: cfg/dominators/loops are rebuilt
        # fresh against the rewritten instructions before the next pick.

    return changed


def reduce_induction_strength(function):
    """Replace ``base + i`` address recomputation with an incremental accumulator.

    Only handles the shape that can be proven correct without phi nodes: a
    basic induction variable ``i``, defined once outside the loop (its entry
    value) and updated by exactly one ``binary(i, step_const, '+'|'-')`` whose
    result flows into the next iteration; a derived address
    ``binary(base, i, '+')`` with a loop-invariant ``base``; and the update
    instruction *dominates* every use of that address within the loop body (so
    every use in an iteration sees the same ``i`` the update produced for it).

    Under that ordering constraint the address only ever changes by ``step``
    between one use and the next, so it is computed once in the preheader from
    ``i``'s entry value and bumped by ``step`` immediately after each update,
    instead of re-adding ``base`` every time.

    ROADMAP: this dominance-ordering restriction (and the single-update-site
    requirement) exists only because the IR has no phi nodes to merge an
    induction variable's preheader and back-edge values. Once full SSA lands
    (see project-dyncc-optimizer-roadmap memory), replace this with a general
    phi-driven induction-variable pass that also handles uses preceding the
    update and multiple update sites per header.
    """
    changed = False
    # As in hoist_loop_invariants: rewriting shifts block indices, so each
    # qualifying loop is handled against a freshly rebuilt CFG/dominators,
    # one loop per snapshot, skipping headers already tried this call.
    skip_headers = set()
    while True:
        cfg = build_cfg(function)
        if not cfg.blocks:
            break
        dominators = compute_dominators(cfg)
        loops = [
            loop
            for loop in find_natural_loops(cfg, dominators)
            if id(cfg.blocks[loop.header].instructions[0]) not in skip_headers
        ]
        if not loops:
            break

        definitions = _definitions(function)
        single = {value: items[0] for value, items in definitions.items() if len(items) == 1}
        constants = _constant_definitions(function)
        block_of = {}
        index_of = {}
        for block in cfg.blocks:
            for position, instruction in enumerate(block.instructions):
                block_of[id(instruction)] = block.index
                index_of[id(instruction)] = position

        def dominates_within_block(a_block, a_pos, b_block, b_pos):
            """True if instruction a dominates instruction b (both given as block/position)."""
            if a_block == b_block:
                return a_pos <= b_pos
            return dominators.dominates(a_block, b_block)

        loop = min(loops, key=lambda l: len(l.blocks))
        header_key = id(cfg.blocks[loop.header].instructions[0])
        preheader_predecessors = [
            p for p in cfg.blocks[loop.header].predecessors if p not in loop.blocks
        ]
        if len(preheader_predecessors) != 1:
            skip_headers.add(header_key)
            continue
        preheader = preheader_predecessors[0]

        # Basic induction variables: exactly one definition inside the loop,
        # of the form i = i +/- step_const, plus exactly one definition
        # outside the loop giving its entry value.
        candidates = {}
        for index in loop.blocks:
            for instruction in cfg.blocks[index].instructions:
                if instruction.op != "binary" or instruction.extra not in ("+", "-"):
                    continue
                left, right = instruction.args
                if left != instruction.dst:
                    continue
                step = constants.get(right)
                if step is None:
                    continue
                dst_defs = [
                    d for d in definitions.get(instruction.dst, []) if d is not instruction
                ]
                in_loop_defs = [d for d in dst_defs if block_of.get(id(d)) in loop.blocks]
                out_defs = [d for d in dst_defs if block_of.get(id(d)) not in loop.blocks]
                if in_loop_defs or len(out_defs) != 1:
                    continue  # not a simple single-update loop counter
                if instruction.dst in candidates:
                    candidates[instruction.dst] = None
                    continue
                signed_step = step if instruction.extra == "+" else -step
                candidates[instruction.dst] = (instruction, signed_step, out_defs[0])
        candidates = {k: v for k, v in candidates.items() if v is not None}
        if not candidates:
            skip_headers.add(header_key)
            continue

        rewrites = []
        for index in sorted(loop.blocks):
            for instruction in cfg.blocks[index].instructions:
                if instruction.op != "binary" or instruction.extra != "+":
                    continue
                if instruction.dst not in single or single[instruction.dst] is not instruction:
                    continue
                left, right = instruction.args
                for base, induction_var in ((left, right), (right, left)):
                    if induction_var not in candidates:
                        continue
                    base_def = single.get(base)
                    if base_def is None or block_of.get(id(base_def)) in loop.blocks:
                        continue  # base must be defined outside the loop
                    update_instr, signed_step, entry_def = candidates[induction_var]
                    if id(instruction) == id(update_instr):
                        continue
                    use_block, use_pos = block_of[id(instruction)], index_of[id(instruction)]
                    update_block = block_of[id(update_instr)]
                    update_pos = index_of[id(update_instr)]
                    # The update must dominate this use so every use in an
                    # iteration observes the value the update just produced
                    # (never a stale value from before the update ran).
                    if not dominates_within_block(update_block, update_pos, use_block, use_pos):
                        continue
                    rewrites.append((instruction, base, entry_def, update_instr, signed_step))
                    break

        if not rewrites:
            skip_headers.add(header_key)
            continue

        preamble = []
        acc0 = function.values
        function.values += 1
        address_instruction, base, entry_def, update_instr, signed_step = rewrites[0]
        preamble.append(
            Instruction("binary", acc0, (base, entry_def.dst), address_instruction.type, "+")
        )
        step_value = function.values
        function.values += 1
        preamble.append(
            Instruction("const", step_value, (), address_instruction.type, abs(signed_step))
        )
        bump = Instruction(
            "binary",
            acc0,
            (acc0, step_value),
            address_instruction.type,
            "+" if signed_step >= 0 else "-",
        )

        replace_ids = {id(address_instruction)}
        for other, other_base, other_entry, other_update, other_step in rewrites[1:]:
            if (
                other_base != base
                or id(other_entry) != id(entry_def)
                or id(other_update) != id(update_instr)
                or other_step != signed_step
            ):
                continue
            replace_ids.add(id(other))

        # Insert before the preheader's terminator (if any) so the preamble
        # stays reachable, rather than after it where it would be dead code.
        last = cfg.blocks[preheader].instructions[-1]
        last_id = id(last)
        insert_before_last = last.op in TERMINATORS
        update_id = id(update_instr)
        rewritten = []
        for instruction in function.instructions:
            if id(instruction) in replace_ids:
                rewritten.append(Instruction("copy", instruction.dst, (acc0,), instruction.type))
                if id(instruction) == update_id:
                    rewritten.append(bump)
                continue
            if insert_before_last and id(instruction) == last_id:
                rewritten.extend(preamble)
            rewritten.append(instruction)
            if not insert_before_last and id(instruction) == last_id:
                rewritten.extend(preamble)
            if id(instruction) == update_id:
                rewritten.append(bump)
        function.instructions = rewritten
        changed = True
        # Loop back to the top of the while: cfg/dominators/loops are rebuilt
        # fresh against the rewritten instructions before the next pick.

    return changed


def eliminate_redundant_loop_memory(function):
    """Eliminate redundant loads/stores to the same address within a loop.

    Same as ``hoist_loop_invariants``, the IR carries no alias information, so
    this only reasons about a loop with *no calls* (a call could read or write
    anything) and treats two memory ops as touching the same location only
    when their addresses are provably identical: either the literal same SSA
    value, or the same structural address computation per ``_address_keys``
    (there is no CSE pass in this compiler, so e.g. two independent lowerings
    of ``arr[5]`` produce distinct SSA values for the same address — without
    this, the rewrite below would essentially never fire on real code).

    Two rewrites, both requiring the earlier op to dominate the later one so
    every path to the later op has already executed the earlier one:
      - load-after-store/load: a load whose address was already loaded or
        stored (with the same value) earlier on every path becomes a copy of
        that earlier value.
      - store-after-store: a store whose address was already stored on every
        path since, with no intervening load of that address, is dead and is
        removed (the earlier store's value is never observed).

    ROADMAP: this dominance-based, no-calls-allowed scoping is a GVN-lite
    stand-in for real alias analysis. Once full SSA lands (see
    project-dyncc-optimizer-roadmap memory), replace with a proper memory-SSA
    or points-to-based redundant load/store elimination that also handles
    loops containing calls to functions proven not to alias the address.
    """
    changed = False
    skip_headers = set()
    while True:
        cfg = build_cfg(function)
        if not cfg.blocks:
            break
        dominators = compute_dominators(cfg)
        loops = [
            loop
            for loop in find_natural_loops(cfg, dominators)
            if id(cfg.blocks[loop.header].instructions[0]) not in skip_headers
        ]
        if not loops:
            break

        block_of = {}
        index_of = {}
        for block in cfg.blocks:
            for position, instruction in enumerate(block.instructions):
                block_of[id(instruction)] = block.index
                index_of[id(instruction)] = position

        def dominates_within_block(a_block, a_pos, b_block, b_pos):
            if a_block == b_block:
                return a_pos <= b_pos
            return dominators.dominates(a_block, b_block)

        definitions = _definitions(function)
        single = {value: items[0] for value, items in definitions.items() if len(items) == 1}
        address_key = _address_keys(function, single)

        loop = min(loops, key=lambda l: len(l.blocks))
        header_key = id(cfg.blocks[loop.header].instructions[0])
        has_calls = any(
            instruction.op in ("call", "direct_call", "tailcall", "direct_tailcall", "intrinsic")
            for index in loop.blocks
            for instruction in cfg.blocks[index].instructions
        )
        if has_calls:
            skip_headers.add(header_key)
            continue

        # Memory ops to this address, in program order, restricted to this loop.
        by_address = {}
        for index in sorted(loop.blocks):
            for instruction in cfg.blocks[index].instructions:
                if instruction.op == "load":
                    by_address.setdefault(address_key(instruction.args[0]), []).append(instruction)
                elif instruction.op == "store":
                    by_address.setdefault(address_key(instruction.args[0]), []).append(instruction)

        redundant_loads = {}  # id(load) -> replacement value
        dead_stores = set()  # id(store)
        for address, ops in by_address.items():
            for later_pos, later in enumerate(ops):
                if later.op != "load" or id(later) in redundant_loads:
                    continue
                later_block, later_index = block_of[id(later)], index_of[id(later)]
                for earlier in reversed(ops[:later_pos]):
                    if id(earlier) in dead_stores:
                        continue
                    earlier_block, earlier_index = block_of[id(earlier)], index_of[id(earlier)]
                    if not dominates_within_block(
                        earlier_block, earlier_index, later_block, later_index
                    ):
                        continue
                    value = earlier.dst if earlier.op == "load" else earlier.args[1]
                    redundant_loads[id(later)] = value
                    break

        for address, ops in by_address.items():
            for earlier_pos, earlier in enumerate(ops):
                if earlier.op != "store" or earlier_pos + 1 >= len(ops):
                    continue
                earlier_block, earlier_index = block_of[id(earlier)], index_of[id(earlier)]
                # The next op to this address in program order: if it's a
                # store that this store dominates, no load ever observes
                # `earlier`'s value (the next store always overwrites it
                # first), so `earlier` is dead.
                later = ops[earlier_pos + 1]
                if later.op != "store" or id(later) in dead_stores:
                    continue
                later_block, later_index = block_of[id(later)], index_of[id(later)]
                if dominates_within_block(
                    earlier_block, earlier_index, later_block, later_index
                ):
                    dead_stores.add(id(earlier))

        if not redundant_loads and not dead_stores:
            skip_headers.add(header_key)
            continue

        rewritten = []
        for instruction in function.instructions:
            if id(instruction) in dead_stores:
                continue
            if id(instruction) in redundant_loads:
                rewritten.append(
                    Instruction("copy", instruction.dst, (redundant_loads[id(instruction)],), instruction.type)
                )
                continue
            rewritten.append(instruction)
        function.instructions = rewritten
        changed = True
        # Loop back to the top of the while: cfg/dominators/loops are rebuilt
        # fresh against the rewritten instructions before the next pick.

    return changed


def _normalize(value, type_):
    if type_.kind not in ("int", "bool", "pointer"):
        return value
    if type_.kind == "bool":
        return int(bool(value))
    bits = type_.size * 8
    mask = (1 << bits) - 1
    value &= mask
    if type_.kind == "int" and type_.signed and value & (1 << (bits - 1)):
        value -= 1 << bits
    return value


def _fold_unary(operator, value, type_):
    if operator == "!":
        return int(not value)
    if operator == "-":
        return _normalize(-value, type_)
    if operator == "~":
        return _normalize(~value, type_)
    return None


def _fold_binary(operator, left, right, type_):
    bits = type_.size * 8
    mask = (1 << bits) - 1
    unsigned_left, unsigned_right = left & mask, right & mask
    if operator == "+":
        value = left + right
    elif operator == "-":
        value = left - right
    elif operator == "*":
        value = left * right
    elif operator in ("/", "%"):
        if right == 0:
            return None
        if type_.signed:
            quotient = abs(left) // abs(right)
            if (left < 0) != (right < 0):
                quotient = -quotient
            value = quotient if operator == "/" else left - quotient * right
        else:
            value = (
                unsigned_left // unsigned_right
                if operator == "/"
                else unsigned_left % unsigned_right
            )
    elif operator == "&":
        value = unsigned_left & unsigned_right
    elif operator == "|":
        value = unsigned_left | unsigned_right
    elif operator == "^":
        value = unsigned_left ^ unsigned_right
    elif operator in ("<<", ">>"):
        if not 0 <= right < bits:
            return None
        if operator == "<<":
            value = unsigned_left << right
        elif type_.signed:
            value = left >> right
        else:
            value = unsigned_left >> right
    elif operator in ("==", "!=", "<", "<=", ">", ">="):
        a, b = (left, right) if type_.signed else (unsigned_left, unsigned_right)
        value = {
            "==": a == b,
            "!=": a != b,
            "<": a < b,
            "<=": a <= b,
            ">": a > b,
            ">=": a >= b,
        }[operator]
    else:
        return None
    return _normalize(int(value), type_)


def propagate_and_fold(function):
    """Propagate copies/constants and fold expressions within basic blocks."""
    constants = {}
    aliases = {}
    definition_counts = {
        value: len(items) for value, items in _definitions(function).items()
    }
    value_types = {
        value: items[0].type
        for value, items in _definitions(function).items()
        if items and all(item.type == items[0].type for item in items)
    }
    rewritten = []

    def resolve(value):
        seen = set()
        while value in aliases and value not in seen:
            seen.add(value)
            value = aliases[value]
        return value

    def invalidate(value):
        for alias in list(aliases):
            current = alias
            seen = set()
            while current in aliases and current not in seen:
                seen.add(current)
                current = aliases[current]
                if current == value:
                    aliases.pop(alias, None)
                    break
        aliases.pop(value, None)

    def barrier():
        # Constants attached to immutable SSA-like values remain valid. Mutable
        # promoted locals and block-local aliases must be rediscovered.
        aliases.clear()
        for value in list(constants):
            if definition_counts.get(value, 0) != 1:
                constants.pop(value)

    for instruction in function.instructions:
        if instruction.op == "label":
            barrier()
        args = tuple(resolve(value) for value in instruction.args)
        instruction = Instruction(
            instruction.op,
            instruction.dst,
            args,
            instruction.type,
            instruction.extra,
        )
        if instruction.dst is not None:
            # Copies from mutable promoted locals are valid snapshots only until
            # that local is assigned again. Preserve the defining copy when a
            # later use needs the old value (notably postfix increment).
            invalidate(instruction.dst)
        if (
            instruction.op == "cast"
            and args[0] in value_types
            and value_types[args[0]].size == instruction.type.size
        ):
            instruction = Instruction(
                "copy", instruction.dst, args, instruction.type
            )
        result = None
        if instruction.op == "const":
            result = _normalize(instruction.extra, instruction.type)
        elif instruction.op == "copy" and args:
            if args[0] in constants:
                result = _normalize(constants[args[0]], instruction.type)
            elif definition_counts.get(instruction.dst, 0) == 1:
                aliases[instruction.dst] = args[0]
        elif instruction.op == "cast" and args[0] in constants:
            result = _normalize(constants[args[0]], instruction.type)
        elif instruction.op == "unary" and args[0] in constants:
            result = _fold_unary(
                instruction.extra, constants[args[0]], instruction.type
            )
        elif instruction.op == "binary" and all(v in constants for v in args):
            result = _fold_binary(
                instruction.extra,
                constants[args[0]],
                constants[args[1]],
                instruction.type,
            )
        if result is not None:
            instruction = Instruction(
                "const", instruction.dst, (), instruction.type, result
            )
            constants[instruction.dst] = result
            aliases.pop(instruction.dst, None)
        elif instruction.dst is not None:
            constants.pop(instruction.dst, None)
            if definition_counts.get(instruction.dst, 0) != 1:
                aliases.pop(instruction.dst, None)
        rewritten.append(instruction)
        if instruction.op in ("jump", "branch_if", "cbranch_if", "return"):
            barrier()
    function.instructions = rewritten


def propagate_global_copies(function):
    """Collapse immutable copy/cast chains across basic-block boundaries."""
    definitions = _definitions(function)
    aliases = {}
    for value, items in definitions.items():
        if len(items) != 1 or items[0].op not in ("copy", "cast"):
            continue
        source = items[0].args[0]
        if len(definitions.get(source, ())) == 1 and (
            items[0].op == "copy"
            or definitions[source][0].type.size == items[0].type.size
        ):
            aliases[value] = source

    def resolve(value):
        seen = set()
        while value in aliases and value not in seen:
            seen.add(value)
            value = aliases[value]
        return value

    changed = False
    rewritten = []
    for instruction in function.instructions:
        args = tuple(resolve(value) for value in instruction.args)
        if args != instruction.args:
            changed = True
            instruction = Instruction(
                instruction.op, instruction.dst, args, instruction.type, instruction.extra
            )
        rewritten.append(instruction)
    function.instructions = rewritten
    return changed


def sparse_conditional_constant_propagation(function):
    """Propagate constants through CFG joins and discover executable edges.

    Promoted mutable virtuals are treated as SSA variables internally: each
    predecessor contributes its outgoing version and the meet at a join is the
    corresponding phi value.  The temporary SSA/phi state is rewritten back to
    ordinary IR constants and branches, so the backend needs no phi lowering.
    """
    cfg = build_cfg(function)
    if not cfg.blocks:
        return False
    unknown, varying = object(), object()
    incoming = [{} for _ in cfg.blocks]
    outgoing = [{} for _ in cfg.blocks]
    reachable = {0}
    executable_edges = set()

    def meet(values):
        result = unknown
        for value in values:
            if value is unknown:
                continue
            if result is unknown:
                result = value
            elif result is varying or value is varying or result != value:
                return varying
        return result

    def evaluate(instruction, env):
        if instruction.op == "const":
            return _normalize(instruction.extra, instruction.type)
        args = [env.get(value, unknown) for value in instruction.args]
        if instruction.op == "copy" and args:
            return args[0]
        if instruction.op == "cast" and args and args[0] not in (unknown, varying):
            return _normalize(args[0], instruction.type)
        if instruction.op == "unary" and args and args[0] not in (unknown, varying):
            folded = _fold_unary(instruction.extra, args[0], instruction.type)
            return varying if folded is None else folded
        if instruction.op == "binary" and all(v not in (unknown, varying) for v in args):
            folded = _fold_binary(instruction.extra, args[0], args[1], instruction.type)
            return varying if folded is None else folded
        return varying if instruction.dst is not None else unknown

    changed = True
    while changed:
        changed = False
        for block in cfg.blocks:
            if block.index not in reachable:
                continue
            if block.index:
                predecessors = [
                    outgoing[p]
                    for p in block.predecessors
                    if (p, block.index) in executable_edges
                ]
                if not predecessors:
                    continue
                keys = set().union(*(state.keys() for state in predecessors))
                state = {
                    key: meet([item.get(key, unknown) for item in predecessors])
                    for key in keys
                }
            else:
                state = {}
            if state != incoming[block.index]:
                incoming[block.index] = state
                changed = True
            env = dict(state)
            for instruction in block.instructions:
                if instruction.dst is not None:
                    env[instruction.dst] = evaluate(instruction, env)
            if env != outgoing[block.index]:
                outgoing[block.index] = env
                changed = True

            last = next((i for i in reversed(block.instructions) if i.op != "label"), None)
            successors = set(block.successors)
            if last is not None and last.op in ("branch_if", "cbranch_if"):
                if last.op == "branch_if":
                    condition = env.get(last.args[0], unknown)
                    result = bool(condition) if condition not in (unknown, varying) else condition
                else:
                    values = [env.get(v, unknown) for v in last.args]
                    result = (
                        bool(_fold_binary(last.extra[0], values[0], values[1], last.type))
                        if all(v not in (unknown, varying) for v in values)
                        else varying if varying in values else unknown
                    )
                if result not in (unknown, varying):
                    target = cfg.label_blocks[last.extra[1]]
                    taken = result == (last.extra[0] if last.op == "branch_if" else True)
                    successors = {target} if taken else successors - {target}
                elif result is unknown:
                    successors = set()
            for successor in successors:
                edge = (block.index, successor)
                if edge not in executable_edges:
                    executable_edges.add(edge)
                    reachable.add(successor)
                    changed = True

    original = tuple(function.instructions)
    rewritten = []
    for block in cfg.blocks:
        if block.index not in reachable:
            continue
        env = dict(incoming[block.index])
        for instruction in block.instructions:
            value = evaluate(instruction, env) if instruction.dst is not None else unknown
            if instruction.dst is not None:
                env[instruction.dst] = value
                if (
                    value not in (unknown, varying)
                    and instruction.op in ("copy", "cast", "unary", "binary")
                ):
                    instruction = Instruction(
                        "const", instruction.dst, (), instruction.type, value
                    )
            if instruction.op in ("branch_if", "cbranch_if"):
                live = {
                    successor
                    for predecessor, successor in executable_edges
                    if predecessor == block.index
                }
                target = cfg.label_blocks[instruction.extra[1]]
                if target not in live:
                    continue
                if len(live) == 1:
                    instruction = Instruction("jump", extra=instruction.extra[1])
            rewritten.append(instruction)
    function.instructions = rewritten
    return tuple(function.instructions) != original


def simplify_control_flow(function):
    """Fold constant branches and discard unreachable instructions/blocks."""
    original = tuple(function.instructions)
    changed = True
    while changed:
        changed = False
        constants = _constant_definitions(function)
        instructions = []
        dead = False
        for instruction in function.instructions:
            if instruction.op == "label":
                dead = False
            if dead and instruction.op != "label":
                changed = True
                continue
            if instruction.op == "branch_if" and instruction.args[0] in constants:
                truthy, target = instruction.extra
                if bool(constants[instruction.args[0]]) == truthy:
                    instruction = Instruction("jump", extra=target)
                    dead = True
                else:
                    changed = True
                    continue
                changed = True
            elif instruction.op == "cbranch_if" and all(
                value in constants for value in instruction.args
            ):
                operator = instruction.extra[0]
                result = _fold_binary(
                    operator,
                    constants[instruction.args[0]],
                    constants[instruction.args[1]],
                    instruction.type,
                )
                if result:
                    instruction = Instruction("jump", extra=instruction.extra[1])
                    dead = True
                else:
                    changed = True
                    continue
                changed = True
            instructions.append(instruction)
            if instruction.op in (
                "jump",
                "return",
                "tailcall",
                "direct_tailcall",
                "halt",
            ):
                dead = True

        # Keep even unreferenced labels here. Besides naming branch targets, labels
        # delimit fallthrough blocks after a terminated predecessor; deleting one
        # before building a full CFG could make the next cleanup iteration erase a
        # reachable fallthrough block.
        result = []
        for index, instruction in enumerate(instructions):
            if instruction.op == "jump":
                following_labels = set()
                cursor = index + 1
                while cursor < len(instructions) and instructions[cursor].op == "label":
                    following_labels.add(instructions[cursor].extra)
                    cursor += 1
                if instruction.extra in following_labels:
                    changed = True
                    continue
            result.append(instruction)
        function.instructions = result
        if prune_unreachable_blocks(function):
            changed = True
        referenced_labels = set()
        for instruction in function.instructions:
            if instruction.op == "jump":
                referenced_labels.add(instruction.extra)
            elif instruction.op in ("branch_if", "cbranch_if"):
                referenced_labels.add(instruction.extra[1])
        without_unused_labels = [
            instruction
            for instruction in function.instructions
            if instruction.op != "label" or instruction.extra in referenced_labels
        ]
        if len(without_unused_labels) != len(function.instructions):
            function.instructions = without_unused_labels
            changed = True
    return tuple(function.instructions) != original


def thread_jumps(function):
    """Redirect edges through blocks containing only labels and one jump."""
    cfg = build_cfg(function)
    redirects = {}
    for block in cfg.blocks:
        operations = [item for item in block.instructions if item.op != "label"]
        if len(operations) == 1 and operations[0].op == "jump":
            for label in block.labels:
                if label != operations[0].extra:
                    redirects[label] = operations[0].extra
    if not redirects:
        return False

    def resolve(label):
        seen = set()
        while label in redirects and label not in seen:
            seen.add(label)
            label = redirects[label]
        return label

    changed = False
    rewritten = []
    for instruction in function.instructions:
        extra = instruction.extra
        if instruction.op == "jump":
            extra = resolve(extra)
        elif instruction.op in ("branch_if", "cbranch_if"):
            extra = (extra[0], resolve(extra[1]))
        if extra != instruction.extra:
            changed = True
            instruction = Instruction(
                instruction.op,
                instruction.dst,
                instruction.args,
                instruction.type,
                extra,
            )
        rewritten.append(instruction)
    function.instructions = rewritten
    return changed


def fuse_comparison_branches(function):
    """Keep comparison results in flags when their sole use is a branch."""
    definitions = _definitions(function)
    uses = {}
    for instruction in function.instructions:
        for value in instruction.args:
            uses.setdefault(value, []).append(instruction)
    fused = set()
    rewritten = []
    comparisons = {"==", "!=", "<", "<=", ">", ">="}
    for instruction in function.instructions:
        if instruction.op == "branch_if":
            value = instruction.args[0]
            items = definitions.get(value, ())
            if (
                len(items) == 1
                and items[0].op == "binary"
                and items[0].extra in comparisons
                and uses.get(value) == [instruction]
            ):
                comparison = items[0]
                fused.add(id(comparison))
                operator = comparison.extra
                if not instruction.extra[0]:
                    operator = {
                        "==": "!=",
                        "!=": "==",
                        "<": ">=",
                        "<=": ">",
                        ">": "<=",
                        ">=": "<",
                    }[operator]
                instruction = Instruction(
                    "cbranch_if",
                    args=comparison.args,
                    type=comparison.type,
                    extra=(operator, instruction.extra[1]),
                )
        rewritten.append(instruction)
    before = tuple(function.instructions)
    function.instructions = [
        instruction for instruction in rewritten if id(instruction) not in fused
    ]
    return tuple(function.instructions) != before


def inline_single_call_functions(module):
    """Relocate non-recursive functions having exactly one direct call site."""
    functions = {function.name: function for function in module.functions}
    call_sites = {name: [] for name in functions}
    direct_edges = {name: set() for name in functions}
    observable_addresses = {
        symbol
        for global_ in module.globals
        for _, symbol, _ in global_.relocations
        if symbol in functions
    }
    for caller in module.functions:
        definitions = {
            instruction.dst: instruction
            for instruction in caller.instructions
            if instruction.dst is not None
        }
        uses = {}
        for instruction in caller.instructions:
            for position, value in enumerate(instruction.args):
                uses.setdefault(value, []).append((instruction, position))
        for instruction in caller.instructions:
            if (
                instruction.op in ("direct_call", "direct_tailcall")
                and instruction.extra in functions
            ):
                call_sites[instruction.extra].append((caller, instruction))
                direct_edges[caller.name].add(instruction.extra)
        for value, definition in definitions.items():
            if definition.op != "global_addr" or definition.extra not in functions:
                continue
            for user, position in uses.get(value, ()):
                observable_addresses.add(definition.extra)

    def reaches(start, target):
        pending = list(direct_edges[start])
        seen = set()
        while pending:
            name = pending.pop()
            if name == target:
                return True
            if name in seen:
                continue
            seen.add(name)
            pending.extend(direct_edges.get(name, ()))
        return False

    candidates = {}
    for function in module.functions:
        caller = (
            call_sites[function.name][0][0]
            if len(call_sites[function.name]) == 1
            else None
        )
        if (
            function.name != "_start"
            and function.name not in observable_addresses
            and caller is not None
            and caller is not function
            and not reaches(function.name, caller.name)
            and not any(
                i.op in ("startup", "halt")
                for i in function.instructions
            )
        ):
            candidates[function.name] = function

    changed = False
    inline_id = getattr(module, "_inline_serial", 0)
    for caller in module.functions:
        definitions = {
            instruction.dst: instruction
            for instruction in caller.instructions
            if instruction.dst is not None
        }
        rewritten = []
        for call_instruction in caller.instructions:
            if call_instruction.op != "direct_call":
                rewritten.append(call_instruction)
                continue
            callee = candidates.get(call_instruction.extra)
            if callee is None or callee is caller:
                rewritten.append(call_instruction)
                continue

            changed = True
            inline_id += 1
            module._inline_serial = inline_id
            base = caller.values
            mapping = {value: base + value for value in range(callee.values)}
            caller.values += callee.values
            call_arguments = call_instruction.args
            labels = {
                instruction.extra: f"{caller.name}.inline{inline_id}.{instruction.extra}"
                for instruction in callee.instructions
                if instruction.op == "label"
            }
            continuation = f"{caller.name}.inline{inline_id}.return"
            promoted_parameters = {
                instruction.extra[1]
                for instruction in callee.instructions
                if instruction.op == "param"
            }
            caller.locals.extend(callee.params)
            caller.locals.extend(callee.locals)
            for index, parameter in enumerate(callee.params):
                if parameter.key in promoted_parameters:
                    continue
                address = caller.values
                caller.values += 1
                rewritten.append(
                    Instruction(
                        "local_addr", address, (), pointer(parameter.type), parameter.key
                    )
                )
                rewritten.append(
                    Instruction(
                        "store", args=(address, call_arguments[index]), type=parameter.type
                    )
                )

            def remap_extra(instruction):
                if instruction.op in ("label", "jump"):
                    return labels[instruction.extra]
                if instruction.op in ("branch_if", "cbranch_if"):
                    return (instruction.extra[0], labels[instruction.extra[1]])
                return instruction.extra

            for instruction in callee.instructions:
                if instruction.op == "param":
                    mapping[instruction.dst] = call_arguments[instruction.extra[0] - 1]
                    continue
                if instruction.op in ("tailcall", "direct_tailcall"):
                    result = caller.values
                    caller.values += 1
                    rewritten.append(
                        Instruction(
                            "call" if instruction.op == "tailcall" else "direct_call",
                            result if instruction.type.kind != "void" else None,
                            tuple(mapping[value] for value in instruction.args),
                            instruction.type,
                            instruction.extra,
                        )
                    )
                    if call_instruction.dst is not None and instruction.type.kind != "void":
                        rewritten.append(
                            Instruction(
                                "copy",
                                call_instruction.dst,
                                (result,),
                                call_instruction.type,
                            )
                        )
                    rewritten.append(Instruction("jump", extra=continuation))
                    continue
                if instruction.op == "return":
                    if call_instruction.dst is not None and instruction.args:
                        rewritten.append(
                            Instruction(
                                "copy",
                                call_instruction.dst,
                                (mapping[instruction.args[0]],),
                                call_instruction.type,
                            )
                        )
                    rewritten.append(Instruction("jump", extra=continuation))
                    continue
                rewritten.append(
                    Instruction(
                        instruction.op,
                        mapping[instruction.dst] if instruction.dst is not None else None,
                        tuple(mapping[value] for value in instruction.args),
                        instruction.type,
                        remap_extra(instruction),
                    )
                )
            rewritten.append(Instruction("label", extra=continuation))
        caller.instructions = rewritten
    return changed


def eliminate_tail_calls(function):
    """Turn calls whose continuation only returns their result into tail calls."""
    instructions = function.instructions
    local_keys = {symbol.key for symbol in function.locals}
    if any(
        instruction.op == "local_addr" and instruction.extra in local_keys
        for instruction in instructions
    ):
        # A tail callee could receive a pointer into this frame; dismantling and
        # reusing the frame would let its prologue overwrite the pointed object.
        return
    labels = {
        instruction.extra: index
        for index, instruction in enumerate(instructions)
        if instruction.op == "label"
    }

    def continuation(index):
        seen = set()
        while index < len(instructions):
            instruction = instructions[index]
            if instruction.op == "label":
                index += 1
                continue
            if instruction.op == "jump" and instruction.extra not in seen:
                seen.add(instruction.extra)
                index = labels.get(instruction.extra, len(instructions)) + 1
                continue
            return instruction
        return None

    rewritten = []
    for index, instruction in enumerate(instructions):
        if instruction.op in ("call", "direct_call"):
            following = continuation(index + 1)
            argument_count = len(instruction.args) - (instruction.op == "call")
            if (
                argument_count <= 6
                and following is not None
                and following.op == "return"
                and (
                    (not following.args and instruction.type.kind == "void")
                    or following.args == (instruction.dst,)
                )
            ):
                instruction = Instruction(
                    "direct_tailcall" if instruction.op == "direct_call" else "tailcall",
                    args=instruction.args,
                    type=instruction.type,
                    extra=instruction.extra,
                )
        rewritten.append(instruction)
    function.instructions = rewritten


def lower_self_tail_calls_to_loops(function):
    """Turn safe direct self-tail calls into parallel updates and a backedge."""
    recursive = [
        instruction
        for instruction in function.instructions
        if instruction.op == "direct_tailcall" and instruction.extra == function.name
    ]
    if not recursive:
        return False
    parameters = sorted(
        (instruction for instruction in function.instructions if instruction.op == "param"),
        key=lambda instruction: instruction.extra[0],
    )
    if any(len(instruction.args) != len(parameters) for instruction in recursive):
        return False
    labels = {i.extra for i in function.instructions if i.op == "label"}
    loop = f"{function.name}.tail_loop"
    serial = 0
    while loop in labels:
        serial += 1
        loop = f"{function.name}.tail_loop{serial}"

    rewritten = []
    inserted = False
    for instruction in function.instructions:
        if not inserted and instruction.op != "param":
            rewritten.append(Instruction("label", extra=loop))
            inserted = True
        if instruction.op == "direct_tailcall" and instruction.extra == function.name:
            snapshots = []
            for argument, parameter in zip(instruction.args, parameters):
                value = function.values
                function.values += 1
                rewritten.append(
                    Instruction("copy", value, (argument,), parameter.type)
                )
                snapshots.append(value)
            for value, parameter in zip(snapshots, parameters):
                rewritten.append(
                    Instruction("copy", parameter.dst, (value,), parameter.type)
                )
            rewritten.append(Instruction("jump", extra=loop))
        else:
            rewritten.append(instruction)
    if not inserted:
        rewritten.append(Instruction("label", extra=loop))
    function.instructions = rewritten
    return True


def promote_readonly_parameters(function):
    """Turn non-addressed, never-written parameters into ordinary IR values."""
    definitions = {
        instruction.dst: instruction
        for instruction in function.instructions
        if instruction.dst is not None
    }
    users = {}
    for instruction in function.instructions:
        for value in instruction.args:
            users.setdefault(value, []).append(instruction)

    promoted = []
    replacement = {}
    for index, parameter in enumerate(function.params, 1):
        addresses = [
            value
            for value, instruction in definitions.items()
            if instruction.op == "local_addr" and instruction.extra == parameter.key
        ]
        if not addresses:
            continue
        if not all(
            all(user.op == "load" and user.args == (address,) for user in users.get(address, ()))
            for address in addresses
        ):
            continue

        value = function.values
        function.values += 1
        promoted.append(Instruction("param", value, (), parameter.type, (index, parameter.key)))
        for address in addresses:
            for user in users.get(address, ()):
                replacement[id(user)] = Instruction(
                    "copy", user.dst, (value,), user.type
                )

    if promoted:
        function.instructions = promoted + [
            replacement.get(id(instruction), instruction)
            for instruction in function.instructions
        ]


def _constant_definitions(function):
    return {
        value: instructions[0].extra
        for value, instructions in _definitions(function).items()
        if len(instructions) == 1 and instructions[0].op == "const"
    }


def lower_intrinsics(function):
    """Replace direct calls to built-in device functions with target IR ops."""
    rewritten = []
    for instruction in function.instructions:
        if instruction.op == "direct_call" and instruction.extra in INTRINSIC_NAMES:
            instruction = Instruction(
                "intrinsic",
                instruction.dst,
                instruction.args,
                instruction.type,
                instruction.extra,
            )
        rewritten.append(instruction)
    function.instructions = rewritten


def identify_direct_calls(function):
    """Represent calls to known symbols directly instead of through address values."""
    definitions = {
        instruction.dst: instruction
        for instruction in function.instructions
        if instruction.dst is not None
    }
    changed = False
    rewritten = []
    for instruction in function.instructions:
        if instruction.op == "call":
            target = definitions.get(instruction.args[0])
            if target is not None and target.op == "global_addr":
                instruction = Instruction(
                    "direct_call",
                    instruction.dst,
                    instruction.args[1:],
                    instruction.type,
                    target.extra,
                )
                changed = True
        rewritten.append(instruction)
    function.instructions = rewritten
    return changed


def strength_reduce(function):
    """Replace unsigned/pure power-of-two arithmetic with shifts and masks."""
    constants = _constant_definitions(function)
    rewritten = []
    for instruction in function.instructions:
        if instruction.op != "binary":
            rewritten.append(instruction)
            continue

        left, right = instruction.args
        left_constant = constants.get(left)
        right_constant = constants.get(right)

        if instruction.extra == "*":
            if left_constant is not None and right_constant is None:
                left, right = right, left
                right_constant = left_constant
            if right_constant == 0:
                instruction = Instruction(
                    "const", instruction.dst, (), instruction.type, 0
                )
                constants[instruction.dst] = 0
            elif right_constant == 1:
                instruction = Instruction(
                    "copy", instruction.dst, (left,), instruction.type
                )
            elif (
                isinstance(right_constant, int)
                and right_constant > 0
                and right_constant & (right_constant - 1) == 0
            ):
                shift = right_constant.bit_length() - 1
                shift_value = function.values
                function.values += 1
                rewritten.append(
                    Instruction("const", shift_value, (), instruction.type, shift)
                )
                constants[shift_value] = shift
                instruction = Instruction(
                    "binary",
                    instruction.dst,
                    (left, shift_value),
                    instruction.type,
                    "<<",
                )

        # Unsigned division and remainder by powers of two are exact bit ops.
        elif (
            instruction.extra in ("/", "%")
            and not instruction.type.signed
            and isinstance(right_constant, int)
            and right_constant > 0
            and right_constant & (right_constant - 1) == 0
        ):
            if instruction.extra == "/":
                value = right_constant.bit_length() - 1
                operator = ">>"
            else:
                value = right_constant - 1
                operator = "&"
            operand = function.values
            function.values += 1
            rewritten.append(Instruction("const", operand, (), instruction.type, value))
            constants[operand] = value
            instruction = Instruction(
                "binary",
                instruction.dst,
                (left, operand),
                instruction.type,
                operator,
            )

        rewritten.append(instruction)
    function.instructions = rewritten


def simplify_algebra(function):
    """Apply side-effect-safe integer identities to already-evaluated IR values."""
    constants = _constant_definitions(function)
    rewritten = []
    for instruction in function.instructions:
        if instruction.op != "binary":
            rewritten.append(instruction)
            continue
        left, right = instruction.args
        left_constant = constants.get(left)
        right_constant = constants.get(right)
        operator = instruction.extra
        replacement = None

        if right_constant == 0 and operator in ("+", "-", "|", "^", "<<", ">>"):
            replacement = Instruction("copy", instruction.dst, (left,), instruction.type)
        elif left_constant == 0 and operator in ("+", "|", "^"):
            replacement = Instruction("copy", instruction.dst, (right,), instruction.type)
        elif (left_constant == 0 or right_constant == 0) and operator == "&":
            replacement = Instruction("const", instruction.dst, (), instruction.type, 0)
        elif left == right and operator in ("-", "^"):
            replacement = Instruction("const", instruction.dst, (), instruction.type, 0)
        elif left == right and operator in ("&", "|"):
            replacement = Instruction("copy", instruction.dst, (left,), instruction.type)
        elif left == right and operator in ("==", "<=", ">="):
            replacement = Instruction("const", instruction.dst, (), instruction.type, 1)
        elif left == right and operator in ("!=", "<", ">"):
            replacement = Instruction("const", instruction.dst, (), instruction.type, 0)
        elif right_constant == 1 and operator == "/":
            replacement = Instruction("copy", instruction.dst, (left,), instruction.type)
        elif right_constant in (1, -1) and operator == "%":
            replacement = Instruction("const", instruction.dst, (), instruction.type, 0)
        elif operator == "|" and right_constant is not None:
            mask = (1 << (instruction.type.size * 8)) - 1
            if right_constant & mask == mask:
                replacement = Instruction(
                    "const", instruction.dst, (), instruction.type, right_constant & mask
                )
        elif operator == "&" and right_constant is not None:
            mask = (1 << (instruction.type.size * 8)) - 1
            if right_constant & mask == mask:
                replacement = Instruction("copy", instruction.dst, (left,), instruction.type)

        rewritten.append(replacement or instruction)
    function.instructions = rewritten


def remove_dead_values(function):
    """Discard side-effect-free values that cannot affect control flow or memory."""
    required = set()
    keep = set()
    for index, instruction in enumerate(function.instructions):
        if instruction.dst is None or instruction.op in (
            "call",
            "direct_call",
            "tailcall",
            "direct_tailcall",
            "intrinsic",
            "stack_alloc",
        ):
            keep.add(index)
            required.update(instruction.args)

    changed = True
    while changed:
        changed = False
        for index in range(len(function.instructions) - 1, -1, -1):
            instruction = function.instructions[index]
            if instruction.dst in required and index not in keep:
                keep.add(index)
                required.update(instruction.args)
                changed = True

    function.instructions = [
        instruction
        for index, instruction in enumerate(function.instructions)
        if index in keep or instruction.op not in PURE
    ]


def _runtime_helper(instruction, constants):
    if instruction.op != "binary" or instruction.extra not in ("*", "/", "%"):
        return None
    if instruction.extra == "*":
        return "__dyn_mul"
    return (
        "__dyn_"
        + ("s" if instruction.type.signed else "u")
        + ("div" if instruction.extra == "/" else "mod")
    )


def remove_unreachable_symbols(module):
    """Prune closed-world functions and data unreachable from the startup IR.

    A live instruction retains each symbol whose address it materializes.  A live
    global in turn retains the symbols named by its initializer relocations.  The
    combined traversal is important: a function pointer in dead data must not keep
    either the data or its target function in the image.
    """
    before_functions = tuple(function.name for function in module.functions)
    before_globals = tuple(global_.symbol.key for global_ in module.globals)
    functions = {function.name: function for function in module.functions}
    globals_ = {global_.symbol.key: global_ for global_ in module.globals}
    pending_functions = ["_start"]
    pending_globals = []

    reachable_functions = set()
    reachable_globals = set()
    while pending_functions or pending_globals:
        while pending_functions:
            name = pending_functions.pop()
            if name in reachable_functions or name not in functions:
                continue
            reachable_functions.add(name)
            function = functions[name]
            constants = _constant_definitions(function)
            for instruction in function.instructions:
                if instruction.op == "stack_alloc":
                    pending_globals.extend(
                        global_.symbol.key
                        for global_ in module.globals
                        if global_.symbol.name == "__dyn_heap_anchor"
                    )
                if instruction.op in ("direct_call", "direct_tailcall"):
                    pending_functions.append(instruction.extra)
                if instruction.op == "global_addr":
                    if instruction.extra in functions:
                        pending_functions.append(instruction.extra)
                    elif instruction.extra in globals_:
                        pending_globals.append(instruction.extra)
                helper = _runtime_helper(instruction, constants)
                if helper:
                    pending_functions.append(helper)

        while pending_globals:
            name = pending_globals.pop()
            if name in reachable_globals or name not in globals_:
                continue
            reachable_globals.add(name)
            for _, target, _ in globals_[name].relocations:
                if target in functions:
                    pending_functions.append(target)
                elif target in globals_:
                    pending_globals.append(target)

    module.functions = [
        function
        for function in module.functions
        if function.name in reachable_functions
    ]
    module.globals = [
        global_
        for global_ in module.globals
        if global_.symbol.key in reachable_globals
    ]

    # Relocation setup has observable cost but no work once the last live
    # initializer relocation disappears.
    if not any(global_.relocations for global_ in module.globals):
        entry = next(
            (function for function in module.functions if function.name == "_start"),
            None,
        )
        if entry is not None:
            entry.instructions = [
                instruction
                for instruction in entry.instructions
                if instruction.op != "relocate_globals"
            ]

    return (
        tuple(function.name for function in module.functions) != before_functions
        or tuple(global_.symbol.key for global_ in module.globals) != before_globals
    )


def remove_unreachable_functions(module):
    """Compatibility name for the combined whole-program reachability pass."""
    return remove_unreachable_symbols(module)


def fold_immutable_global_loads(module):
    """Fold loads from initialized globals whose addresses cannot be mutated.

    This deliberately uses a closed-world, escape-sensitive proof.  Directly
    derived addresses may participate in address arithmetic and loads.  Passing
    one to an opaque operation, returning it, or storing it as a runtime value
    makes the corresponding object ineligible.
    """
    globals_ = {global_.symbol.key: global_ for global_ in module.globals}
    unsafe = set()

    def facts(function):
        immutable_values = {
            value
            for value, definitions in _definitions(function).items()
            if len(definitions) == 1
        }
        constants = {}
        addresses = {}
        origins = {}
        for instruction in function.instructions:
            if instruction.op == "global_addr" and instruction.extra in globals_:
                origins[instruction.dst] = {instruction.extra}
            elif instruction.op in ("copy", "cast", "binary"):
                inherited = set().union(
                    *(origins.get(argument, set()) for argument in instruction.args)
                )
                if inherited:
                    origins[instruction.dst] = inherited

            if instruction.dst not in immutable_values:
                continue
            if instruction.op == "const":
                constants[instruction.dst] = instruction.extra
            elif instruction.op == "global_addr" and instruction.extra in globals_:
                addresses[instruction.dst] = (instruction.extra, 0)
            elif instruction.op in ("copy", "cast") and instruction.args:
                if instruction.args[0] in addresses:
                    addresses[instruction.dst] = addresses[instruction.args[0]]
            elif instruction.op == "binary" and instruction.extra in ("+", "-"):
                left, right = instruction.args
                if left in addresses and right in constants:
                    symbol, offset = addresses[left]
                    delta = constants[right]
                    addresses[instruction.dst] = (
                        symbol,
                        offset + (delta if instruction.extra == "+" else -delta),
                    )
                elif (
                    instruction.extra == "+"
                    and right in addresses
                    and left in constants
                ):
                    symbol, offset = addresses[right]
                    addresses[instruction.dst] = (symbol, offset + constants[left])
            elif instruction.op == "load" and instruction.args:
                address = addresses.get(instruction.args[0])
                if address is not None:
                    global_ = globals_[address[0]]
                    relocation = next(
                        (
                            (target, addend)
                            for offset, target, addend in global_.relocations
                            if offset == address[1]
                        ),
                        None,
                    )
                    if relocation is not None and instruction.type.kind == "pointer":
                        target, addend = relocation
                        if addend == 0 and target in globals_:
                            addresses[instruction.dst] = (target, 0)
                            origins[instruction.dst] = {target}

        return constants, addresses, origins

    function_facts = []
    for function in module.functions:
        constants, addresses, origins = facts(function)
        function_facts.append((function, constants, addresses))
        for instruction in function.instructions:
            argument_origins = set().union(
                *(
                    origins.get(arg, set())
                    for arg in instruction.args
                )
            )
            if not argument_origins:
                continue
            if instruction.op == "store":
                unsafe.update(origins.get(instruction.args[0], set()))
                if len(instruction.args) > 1:
                    unsafe.update(origins.get(instruction.args[1], set()))
            elif instruction.op not in ("load", "binary", "copy", "cast"):
                unsafe.update(argument_origins)

    changed = False
    for function, _, addresses in function_facts:
        rewritten = []
        for instruction in function.instructions:
            replacement = None
            if instruction.op == "load" and instruction.args[0] in addresses:
                symbol, offset = addresses[instruction.args[0]]
                global_ = globals_[symbol]
                if symbol not in unsafe:
                    relocation = next(
                        (
                            (target, addend)
                            for at, target, addend in global_.relocations
                            if at == offset
                        ),
                        None,
                    )
                    if (
                        relocation is not None
                        and instruction.type.kind == "pointer"
                        and relocation[1] == 0
                    ):
                        replacement = Instruction(
                            "global_addr",
                            instruction.dst,
                            (),
                            instruction.type,
                            relocation[0],
                        )
                    elif (
                        relocation is None
                        and instruction.type.integer
                        and 0 <= offset
                        and offset + instruction.type.size <= len(global_.data)
                    ):
                        raw = int.from_bytes(
                            global_.data[offset : offset + instruction.type.size],
                            "big",
                            signed=False,
                        )
                        if instruction.type.signed:
                            sign = 1 << (instruction.type.size * 8 - 1)
                            raw = (raw ^ sign) - sign
                        replacement = Instruction(
                            "const",
                            instruction.dst,
                            (),
                            instruction.type,
                            raw,
                        )
            rewritten.append(replacement or instruction)
            changed |= replacement is not None
        function.instructions = rewritten
    return changed


def remove_unused_stack_initialization(module):
    """Drop entry stack setup when optimized entry IR cannot touch the stack."""
    entry = next((function for function in module.functions if function.name == "_start"), None)
    if entry is None or not any(i.op == "init_stack" for i in entry.instructions):
        return False
    stack_free = {
        "init_pic",
        "init_stack",
        "relocate_globals",
        "const",
        "global_addr",
        "copy",
        "cast",
        "unary",
        "binary",
        "intrinsic",
        "halt",
    }
    if any(i.op not in stack_free for i in entry.instructions):
        return False
    if any(i.op == "binary" and i.extra in ("*", "/", "%") for i in entry.instructions):
        return False
    entry.instructions = [i for i in entry.instructions if i.op != "init_stack"]
    return True


def _optimize_functions(functions):
    for function in functions:
        promote_readonly_parameters(function)
        lower_intrinsics(function)
        identify_direct_calls(function)
        promote_scalar_locals(function)
        sparse_conditional_constant_propagation(function)
        propagate_global_copies(function)
        propagate_and_fold(function)
        simplify_control_flow(function)
        simplify_algebra(function)
        hoist_loop_invariants(function)
        reduce_induction_strength(function)
        eliminate_redundant_loop_memory(function)
        strength_reduce(function)
        remove_dead_values(function)
        propagate_and_fold(function)
        simplify_control_flow(function)
        remove_dead_values(function)
        fuse_comparison_branches(function)


def optimize(module):
    def state(current):
        functions = tuple(
            (
                function.name,
                function.values,
                tuple(
                    (i.op, i.dst, i.args, i.type, repr(i.extra))
                    for i in function.instructions
                ),
            )
            for function in current.functions
        )
        globals_ = tuple(
            (
                global_.symbol.key,
                bytes(global_.data),
                tuple(global_.relocations),
                global_.reserved,
            )
            for global_ in current.globals
        )
        return functions, globals_

    def local_passes(current):
        _optimize_functions(current.functions)

    def call_graph_passes(current):
        inline_single_call_functions(current)
        for function in current.functions:
            eliminate_tail_calls(function)
            lower_self_tail_calls_to_loops(function)
            thread_jumps(function)
            simplify_control_flow(function)
            remove_dead_values(function)
        remove_unreachable_functions(current)
        fold_immutable_global_loads(current)
        remove_unused_stack_initialization(current)

    return FixedPointPassManager(
        (local_passes, call_graph_passes),
        state,
    ).run(module)
