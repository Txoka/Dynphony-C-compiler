# Dynphony architecture

Machine-level reference for the Dynphony/Symphony ISA. `isa.txt` lists every
instruction spelling and its bit pattern; this document explains the structure
those patterns follow, so an implementation can decode instructions the spec
does not list individually.

## The two ISAs

Symphony and Dynphony share one instruction set, one encoding, and one set of
semantics. They differ only in how much space an instruction occupies:

- **Symphony** is fixed width. Every instruction takes four bytes, whatever its
  encoding needs; shorter instructions are followed by padding that is never
  executed.
- **Dynphony** is the variable-length version of Symphony. An instruction
  occupies only the bytes it encodes — two to four — and the next instruction
  begins immediately after.

The same program bytes mean the same thing in both; only the address of the
next instruction differs. Everything else in this document applies to both.

## Registers

Sixteen 32-bit general registers, `r0`-`r15`.

| Register | Name | Meaning |
| --- | --- | --- |
| r0 | `zr` | Reads as zero. Writes are discarded. |
| r1-r13 | `r1`-`r13` | General purpose. |
| r14 | `sp` | Stack pointer. |
| r15 | `flags` | Comparison flags. |

`zr` is the only register the hardware treats specially: it reads zero and
discards writes. `sp` and `flags` are ordinary registers that happen to carry
those roles **by convention only**. Nothing in the instruction encoding forces
`cmp` to write `flags`, or a jump to read it; both name their register in the
instruction, and an implementation must honour whatever register is named
rather than hard-coding `r15`.

## Instruction format

Instructions are 2 to 4 bytes. Symphony pads every instruction to a fixed four
bytes; Dynphony packs them at their natural length. Immediates are 16-bit,
big-endian, and zero-extended to 32 bits.

```
byte 0    byte 1              byte 2              byte 3
opcode    dest : argument A   argument B  or  immediate (16-bit, big-endian)
          4b     4b           4b (low nibble)
```

- **byte 0** — the opcode.
- **byte 1** — destination register in the high nibble, argument A in the low
  nibble. Instructions that need only one of the two leave the other nibble
  zero; which nibble carries the operand varies by instruction, so consult
  `isa.txt` for the exact placement.
- **bytes 2-3** — either argument B in the low nibble of byte 2, or a 16-bit
  immediate spanning both bytes. The immediate bit in the opcode selects
  between these two, and so also fixes the instruction's length: 3 bytes for
  the register form, 4 for the immediate form.

### Opcode layout

```
bit   7   6   5   4   3   2   1   0
      0 | mode  | I | operation
        |  2b   |1b |    4b
```

- **bit 7** is always zero.
- **bits 6-5** select the mode.
- **bit 4** is the immediate bit: 1 selects the immediate form, 0 the register
  form. Setting it is what turns opcode `0x24` (`add` register) into `0x34`
  (`add` immediate).
- **bits 3-0** select the operation within the mode.

### Modes

| Mode | Value | Opcode range | Contents |
| --- | --- | --- | --- |
| I/O | 0 | `0x00`-`0x1f` | `nop`, `in`, `out`, `keyboard`, `screen`, `time_0`, `time_1`, `counter` |
| ALU | 1 | `0x20`-`0x3f` | `nand`, `or`, `and`, `nor`, `add`, `sub`, `xor`, `lsl`, `lsr`, `asr`, `cmp` |
| Jump | 2 | `0x40`-`0x5f` | every conditional jump, and `jmp` |
| Memory | 3 | `0x60`-`0x7f` | `load_8/16/32`, `pload`, `store_8/16/32`, `pstore` |

Because the operation is just the low nibble, the opcode of any instruction
follows from its mode and its position in the tables above. ALU operation 4 is
`add`, so `add` register is `0x20 + 4 = 0x24` and `add` immediate is `0x34`.

## Flags

A comparison produces a three-bit word:

| Bit | Meaning |
| --- | --- |
| 0 | equal |
| 1 | lower (unsigned `<`) |
| 2 | less (signed `<`) |

`cmp` is an ordinary ALU operation — operation 10 — and this word is simply its
result. It writes that result to the destination register named in the
instruction, with the upper 29 bits zero. The ISA's `cmp` spellings fix the
destination to `flags`, and the compiler always emits it that way, but a `cmp`
encoded with any other destination is well-defined: it writes the flag word
there and leaves `flags` untouched.

Nothing else writes flags implicitly. A register holds a flag word only because
a `cmp` put one there, so an ALU operation or load that targets `flags` simply
overwrites it with its own result, and a later jump reads whatever those bits
now are.

## Jumps

A jump's condition lives entirely in the low nibble of its opcode:

```
bit   3   2   1   0
      N | less | lower | equal
```

Bits 2-0 are a mask selecting which flags to test; bit 3 inverts the result.
Evaluating a jump takes three steps:

1. AND the opcode's low three bits with the low three bits of the flag register
   named by the instruction.
2. Reduce the result: the condition matches if any bit remains set. Hardware
   does this with an OR across the three bits.
3. If opcode bit 3 is set, invert the match.

The jump is taken when the final result is true. Like `cmp`, the flag register
is named in the instruction rather than implied — the ISA's jump spellings all
name `flags`, but an implementation reads whichever register the encoding
gives.

This single rule generates every jump in the instruction set:

| Mnemonic | Opcode | Low nibble | Mask | Invert | Condition |
| --- | --- | --- | --- | --- | --- |
| `je` | `0x41` | `0001` | equal | no | equal |
| `jne` | `0x49` | `1001` | equal | yes | not equal |
| `jb` | `0x42` | `0010` | lower | no | unsigned `<` |
| `jae` | `0x4a` | `1010` | lower | yes | unsigned `>=` |
| `jbe` | `0x43` | `0011` | equal, lower | no | unsigned `<=` |
| `ja` | `0x4b` | `1011` | equal, lower | yes | unsigned `>` |
| `jl` | `0x44` | `0100` | less | no | signed `<` |
| `jge` | `0x4c` | `1100` | less | yes | signed `>=` |
| `jle` | `0x45` | `0101` | equal, less | no | signed `<=` |
| `jg` | `0x4d` | `1101` | equal, less | yes | signed `>` |
| `jmp` | `0x48` | `1000` | none | yes | always |

`jbe` is `je` and `jb` masked together, `jle` is `je` and `jl`, and each
inverted mnemonic is its counterpart with bit 3 set. `jmp` masks nothing, so
the match is always false and the inversion makes it unconditionally taken.

Combinations the table does not name are still valid. Mask `0110` tests lower
or less, and mask `0111` with the invert bit tests "neither equal, lower, nor
less". An implementation must evaluate the formula rather than look up a table
of known mnemonics.

## Program counter

Dynphony advances the PC by the instruction's own length, 2 to 4 bytes — this
variable advance is what distinguishes it from Symphony, which advances by a
fixed 4 bytes regardless of length, leaving any trailing bytes as padding that
is not executed.

A Dynphony instruction's length follows from its opcode: I/O operations carry
their own lengths, and every ALU, jump and memory instruction is 3 bytes in its
register form and 4 in its immediate form, as selected by opcode bit 4.

There is no halt instruction. A program stops by jumping to itself, which
leaves the PC unchanged; an implementation detects that stationary PC and stops
executing.

## Subroutines

`call`, `ret`, `push` and `pop` are not instructions. They are pseudo-
instructions the assembler expands into the sequences shown at the end of
`isa.txt`, built from `counter`, the ALU and memory operations, and `jmp`.

`counter` yields the address of the instruction executing it, so a call
computes its return address by adding the length of the remaining setup to it.
The expansions of `call` and `ret` use `flags` as scratch for that return
address, which is why both clobber it — any flag word in that register does not
survive a call. The Symphony ABI's `link_call` instead uses `r13` as a link
register, leaving both the stack and `flags` alone.
