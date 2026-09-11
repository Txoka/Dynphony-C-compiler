"""Basic-block construction and reachability for Dynphony IR."""

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
