"""Target-independent scalar, control-flow, and reachability optimization."""

from ..ir import Instruction, ModuleIR
from ..intrinsics import NAMES as INTRINSIC_NAMES
from ..model import pointer
from .cfg import build_cfg, prune_unreachable_blocks

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
        if not values or symbol.type.kind not in ("int", "pointer"):
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


def _normalize(value, type_):
    if type_.kind not in ("int", "pointer"):
        return value
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
        if instruction.op in ("jump", "branch", "return"):
            barrier()
    function.instructions = rewritten


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
            if instruction.op == "branch" and instruction.args[0] in constants:
                instruction = Instruction(
                    "jump",
                    extra=instruction.extra[
                        0 if constants[instruction.args[0]] else 1
                    ],
                )
                changed = True
            instructions.append(instruction)
            if instruction.op in ("jump", "return", "tailcall", "halt"):
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
            elif instruction.op == "branch":
                referenced_labels.update(instruction.extra)
            elif instruction.op == "cbranch":
                referenced_labels.update(instruction.extra[1:])
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
        elif instruction.op == "branch":
            extra = tuple(resolve(label) for label in extra)
        elif instruction.op == "cbranch":
            extra = (extra[0],) + tuple(resolve(label) for label in extra[1:])
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
        if instruction.op == "branch":
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
                instruction = Instruction(
                    "cbranch",
                    args=comparison.args,
                    type=comparison.type,
                    extra=(comparison.extra, *instruction.extra),
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
        for value, definition in definitions.items():
            if definition.op != "global_addr" or definition.extra not in functions:
                continue
            for user, position in uses.get(value, ()):
                if user.op == "call" and position == 0:
                    call_sites[definition.extra].append((caller, user))
                else:
                    observable_addresses.add(definition.extra)

    candidates = {}
    for function in module.functions:
        if (
            function.name != "_start"
            and function.name not in observable_addresses
            and len(call_sites[function.name]) == 1
            and call_sites[function.name][0][0] is not function
            and not any(i.op in ("startup", "halt", "tailcall") for i in function.instructions)
        ):
            candidates[function.name] = function

    changed = False
    inline_id = 0
    for caller in module.functions:
        definitions = {
            instruction.dst: instruction
            for instruction in caller.instructions
            if instruction.dst is not None
        }
        rewritten = []
        for call_instruction in caller.instructions:
            if call_instruction.op != "call":
                rewritten.append(call_instruction)
                continue
            target = definitions.get(call_instruction.args[0])
            callee = (
                candidates.get(target.extra)
                if target is not None and target.op == "global_addr"
                else None
            )
            if callee is None or callee is caller:
                rewritten.append(call_instruction)
                continue

            changed = True
            inline_id += 1
            base = caller.values
            mapping = {value: base + value for value in range(callee.values)}
            caller.values += callee.values
            call_arguments = call_instruction.args[1:]
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
                if instruction.op == "branch":
                    return tuple(labels[label] for label in instruction.extra)
                if instruction.op == "cbranch":
                    return (instruction.extra[0],) + tuple(
                        labels[label] for label in instruction.extra[1:]
                    )
                return instruction.extra

            for instruction in callee.instructions:
                if instruction.op == "param":
                    mapping[instruction.dst] = call_arguments[instruction.extra[0] - 1]
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
        if instruction.op == "call":
            following = continuation(index + 1)
            if (
                len(instruction.args) <= 7
                and following is not None
                and following.op == "return"
                and (
                    (not following.args and instruction.type.kind == "void")
                    or following.args == (instruction.dst,)
                )
            ):
                instruction = Instruction(
                    "tailcall", args=instruction.args, type=instruction.type
                )
        rewritten.append(instruction)
    function.instructions = rewritten


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
    definitions = {
        instruction.dst: instruction
        for instruction in function.instructions
        if instruction.dst is not None
    }
    rewritten = []
    for instruction in function.instructions:
        if instruction.op == "call":
            target = definitions.get(instruction.args[0])
            if (
                target is not None
                and target.op == "global_addr"
                and target.extra in INTRINSIC_NAMES
            ):
                instruction = Instruction(
                    "intrinsic",
                    instruction.dst,
                    instruction.args[1:],
                    instruction.type,
                    target.extra,
                )
        rewritten.append(instruction)
    function.instructions = rewritten


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
            "tailcall",
            "intrinsic",
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


def remove_unreachable_functions(module):
    """Keep functions whose address is reachable from the startup IR root."""
    before = tuple(function.name for function in module.functions)
    functions = {function.name: function for function in module.functions}
    pending = ["_start"]
    for global_ in module.globals:
        pending.extend(symbol for _, symbol, _ in global_.relocations)

    reachable = set()
    while pending:
        name = pending.pop()
        if name in reachable or name not in functions:
            continue
        reachable.add(name)
        function = functions[name]
        constants = _constant_definitions(function)
        for instruction in function.instructions:
            if instruction.op == "global_addr" and instruction.extra in functions:
                pending.append(instruction.extra)
            helper = _runtime_helper(instruction, constants)
            if helper:
                pending.append(helper)

    module.functions = [
        function for function in module.functions if function.name in reachable
    ]
    return tuple(function.name for function in module.functions) != before


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
        promote_scalar_locals(function)
        propagate_and_fold(function)
        simplify_control_flow(function)
        simplify_algebra(function)
        strength_reduce(function)
        remove_dead_values(function)
        propagate_and_fold(function)
        simplify_control_flow(function)
        remove_dead_values(function)
        fuse_comparison_branches(function)


def optimize(module):
    def state():
        return tuple(
            (
                function.name,
                function.values,
                tuple(
                    (i.op, i.dst, i.args, i.type, repr(i.extra))
                    for i in function.instructions
                ),
            )
            for function in module.functions
        )

    for _ in range(100):
        before = state()
        _optimize_functions(module.functions)
        inline_single_call_functions(module)
        for function in module.functions:
            eliminate_tail_calls(function)
            thread_jumps(function)
            simplify_control_flow(function)
            remove_dead_values(function)
        remove_unreachable_functions(module)
        remove_unused_stack_initialization(module)
        if state() == before:
            break
    else:
        raise AssertionError("optimizer failed to reach a fixed point")
    return module
