"""Basic-block construction and reachability for Symphony C IR."""

from dataclasses import dataclass, field

from ..ir import Instruction


TERMINATORS = {
    "jump",
    "branch_if",
    "cbranch_if",
    "return",
    "tailcall",
    "direct_tailcall",
    "halt",
}


@dataclass
class BasicBlock:
    """A maximal straight-line instruction sequence."""

    index: int
    instructions: list[Instruction]
    labels: tuple[str, ...] = ()
    successors: set[int] = field(default_factory=set)
    predecessors: set[int] = field(default_factory=set)


@dataclass
class ControlFlowGraph:
    blocks: list[BasicBlock]
    label_blocks: dict[str, int]

    def reachable(self) -> set[int]:
        if not self.blocks:
            return set()
        result = set()
        pending = [0]
        while pending:
            block = pending.pop()
            if block in result:
                continue
            result.add(block)
            pending.extend(self.blocks[block].successors - result)
        return result


def _targets(instruction: Instruction) -> tuple[str, ...]:
    if instruction.op == "jump":
        return (instruction.extra,)
    if instruction.op == "branch_if":
        return (instruction.extra[1],)
    if instruction.op == "cbranch_if":
        return (instruction.extra[1],)
    return ()


def build_cfg(function) -> ControlFlowGraph:
    """Split a linear FunctionIR into blocks and connect its explicit edges."""
    instructions = function.instructions
    if not instructions:
        return ControlFlowGraph([], {})

    leaders = {0}
    for index, instruction in enumerate(instructions):
        if instruction.op == "label":
            leaders.add(index)
        if instruction.op in TERMINATORS and index + 1 < len(instructions):
            leaders.add(index + 1)
    starts = sorted(leaders)
    blocks = []
    label_blocks = {}
    for block_index, start in enumerate(starts):
        end = starts[block_index + 1] if block_index + 1 < len(starts) else len(instructions)
        items = instructions[start:end]
        labels = tuple(item.extra for item in items if item.op == "label")
        block = BasicBlock(block_index, items, labels)
        blocks.append(block)
        for label in labels:
            label_blocks[label] = block_index

    for index, block in enumerate(blocks):
        last = next(
            (item for item in reversed(block.instructions) if item.op != "label"),
            None,
        )
        if last is not None:
            for label in _targets(last):
                if label in label_blocks:
                    block.successors.add(label_blocks[label])
        if index + 1 < len(blocks) and (
            last is None
            or last.op not in TERMINATORS
            or last.op in ("branch_if", "cbranch_if")
        ):
            block.successors.add(index + 1)
        for successor in block.successors:
            blocks[successor].predecessors.add(index)
    return ControlFlowGraph(blocks, label_blocks)


@dataclass
class Dominators:
    """Immediate dominators, dominator tree children, and dominance frontiers.

    Keyed by block index over a :class:`ControlFlowGraph`'s reachable blocks.
    A future SSA construction pass can reuse ``frontiers`` directly to place
    phi nodes, so this is computed once here rather than duplicated per-pass.
    """

    idom: dict[int, int]
    children: dict[int, set[int]]
    frontiers: dict[int, set[int]]

    def dominates(self, a: int, b: int) -> bool:
        while b != a:
            if b not in self.idom:
                return False
            b = self.idom[b]
        return True


def compute_dominators(cfg: ControlFlowGraph) -> Dominators:
    """Compute immediate dominators (iterative, reverse postorder) and frontiers."""
    reachable = cfg.reachable()
    if not reachable:
        return Dominators({}, {}, {})

    order = []
    seen = set()

    def visit(index):
        seen.add(index)
        for successor in cfg.blocks[index].successors:
            if successor not in seen:
                visit(successor)
        order.append(index)

    visit(0)
    postorder = order
    reverse_postorder = list(reversed(postorder))
    position = {index: i for i, index in enumerate(reverse_postorder)}

    idom = {0: 0}
    changed = True
    while changed:
        changed = False
        for index in reverse_postorder:
            if index == 0:
                continue
            predecessors = [
                p for p in cfg.blocks[index].predecessors if p in idom
            ]
            if not predecessors:
                continue
            new_idom = predecessors[0]
            for p in predecessors[1:]:
                a, b = p, new_idom
                while a != b:
                    while position[a] > position[b]:
                        a = idom[a]
                    while position[b] > position[a]:
                        b = idom[b]
                new_idom = a
            if idom.get(index) != new_idom:
                idom[index] = new_idom
                changed = True

    idom.pop(0, None)
    children: dict[int, set[int]] = {index: set() for index in reachable}
    for index, parent in idom.items():
        children.setdefault(parent, set()).add(index)

    frontiers: dict[int, set[int]] = {index: set() for index in reachable}
    for index in reachable:
        predecessors = cfg.blocks[index].predecessors
        if len(predecessors) < 2:
            continue
        for p in predecessors:
            if p not in reachable:
                continue
            runner = p
            while runner != idom.get(index, runner) and runner != index:
                frontiers[runner].add(index)
                if runner not in idom:
                    break
                runner = idom[runner]

    return Dominators(idom, children, frontiers)


@dataclass
class Loop:
    """A natural loop: a header dominating all blocks reachable via a back edge."""

    header: int
    blocks: set[int]
    back_edges: set[tuple[int, int]]


def find_natural_loops(cfg: ControlFlowGraph, dominators: Dominators) -> list[Loop]:
    """Discover natural loops from back edges (edges into a dominating header).

    Loops sharing a header are merged, matching how a single ``for``/``while``
    with multiple continue-like back edges is one loop with one header.
    """
    by_header: dict[int, Loop] = {}
    for block in cfg.blocks:
        for successor in block.successors:
            if not dominators.dominates(successor, block.index):
                continue
            back_edge = (block.index, successor)
            body = {successor}
            stack = [block.index]
            while stack:
                node = stack.pop()
                if node in body:
                    continue
                body.add(node)
                stack.extend(cfg.blocks[node].predecessors)
            if successor in by_header:
                loop = by_header[successor]
                loop.blocks |= body
                loop.back_edges.add(back_edge)
            else:
                by_header[successor] = Loop(successor, body, {back_edge})
    return list(by_header.values())


def prune_unreachable_blocks(function) -> bool:
    """Delete blocks not reachable from the function entry."""
    cfg = build_cfg(function)
    reachable = cfg.reachable()
    if len(reachable) == len(cfg.blocks):
        return False
    function.instructions = [
        instruction
        for block in cfg.blocks
        if block.index in reachable
        for instruction in block.instructions
    ]
    return True
