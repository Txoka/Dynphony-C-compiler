# Dynphony C project and persistent-storage protocol

Dynphony has no filesystem, but a C project needs named translation units,
headers, include roots, and build definitions. The compiler will therefore read
a serialized virtual filesystem called a **Dynphony C Project**, version 1
(DCP1).

This is the proposed stable bootstrap format. A host-side Python packer will
turn a directory into DCP1 and place it in a persistent-storage image. The C
compiler will read that same image in the emulator and on the Dynphony computer.

## Transport

DCP1 is a sequence of unsigned 32-bit words stored in persistent memory. A
`.dcp` file stores those words in big-endian byte order so it can be copied into
the persistent device without translation. The compiler reads words with
`persistent_load(byte_address)` and writes results with
`persistent_store(byte_address, value)`; project files never pass through
`input()`.

Strings and blobs are encoded as:

```text
byte_length: u32
data:        ceil(byte_length / 4) packed words
```

All persistent addresses are byte addresses and are 4-byte aligned. The first
byte occupies bits 31..24 of the first word. The final word is padded
with zero bytes; padding is not part of `byte_length`. Paths use UTF-8, `/` as
the separator, and must be relative, normalized, and free of `.` and `..`
components.

## DCP1 project stream

```text
magic                 u32 = 0x44435031  (ASCII "DCP1")
version               u32 = 1
flags                 u32 = 0
file_count            u32
include_root_count    u32
definition_count      u32

include roots:
  path                string

preprocessor definitions:
  definition          string            (NAME or NAME=VALUE)

files:
  kind                u32               (1 = translation unit, 2 = header)
  path                string
  contents            blob
```

All records are bounded by explicit counts and lengths; there is no end
sentinel. Files are sorted by normalized path by the packer so builds are
deterministic. Duplicate paths, unknown kinds, absolute paths, malformed UTF-8,
and lengths that exceed configured compiler limits are errors. Only records
with kind 1 are compiled; all records are visible to quoted includes, and header
records under configured include roots are visible to angle-bracket includes.

The first packer should default to all `*.c` files as translation units, all
other explicitly included files as headers, `include/` as an include root when
present, and no implicit host headers. A small manifest can later override
those defaults, but it must serialize into the fields above rather than becoming
a second compiler-only project format.

## Persistent compiler control block

At boot, `main` reads a fixed control block beginning at persistent byte address
0. All fields are big-endian 32-bit words:

```text
offset  field
0x00    magic                  = 0x44434331 (ASCII "DCC1")
0x04    version                = 1
0x08    persistent_size        total configured bytes
0x0c    project_address        address of the DCP1 bundle
0x10    project_byte_length
0x14    program_load_address   RAM address for which code is generated
0x18    output_address         destination executable record
0x1c    output_capacity        available destination bytes
0x20    status                 host initializes to 0xffffffff
0x24    output_byte_length     compiler writes actual/required record size
0x28    mode                   bit 0 = run after compile; bit 1 = Symphony output
```

The compiler validates alignment, bounds, and that the control block, project,
and output ranges do not overlap improperly. It changes `status` to
`0xfffffffe` while compiling and writes the final status last, after all output
data and `output_byte_length`. Writing status last makes completion observable
without accepting a partially written result. If the output region is too
small, `output_byte_length` reports the required size.

With bit 0 set, a successful compiler copies the executable record into RAM at
`program_load_address` and jumps to that image's `_start`. The address must not
overlap the compiler image while it is copying. This transfer does not return;
the generated program's return value and halt loop replace the compiler's.
The compiler and generated program must target the same ISA when this bit is
used. Bit 1 selects fixed-width Symphony output; when clear, output uses the
variable-width Dynphony encoding.

`program_load_address` participates in symbol layout and relocation exactly like
the Python compiler's `--load-address`. A loader that places the image at 8192
must request 8192 here. A future target flag may request PIC output, but the
first persistent compiler protocol produces a fixed-address image.

## Compiler output record

On success, `output_address` points directly to the loader-compatible executable
record:

```text
image_byte_length     u32
image                 ceil(image_byte_length / 4) packed words
```

The length is the exact number of meaningful image bytes, excluding final word
padding. This deliberately has no magic or metadata before the length, so a
small loader can read one word, copy the following words into RAM, and execute
them. `output_byte_length` in DCC1 includes the four-byte length prefix and the
padded image storage.

On failure, the first word is zero, followed by a diagnostic blob:

```text
zero                  u32 = 0
diagnostic_length     u32
diagnostic            ceil(diagnostic_length / 4) packed UTF-8 bytes
```

The DCC1 `status` distinguishes success from failure and must be checked before
running the record. `main` also returns it in `r1` for emulator convenience.

## Running the compiler

### Standalone boot

1. Pack the source directory as DCP1.
2. Create a persistent image containing DCC1, the DCP1 bundle, and a reserved
   output region.
3. Build or load the compiler image at its configured RAM address.
4. Attach the preloaded persistent-storage image.
5. Start execution at the image entry, normally byte address 0.
6. The generated `_start` initializes the runtime and calls C `main`.
7. Wait for the DCC1 status field to change from running, then check it and read
   the executable record from the persistent output region.

For a one-shot compile-and-run boot, set mode 1 (the host packer option is
`--run-after-compile`) and choose a non-overlapping program load address. A
successful compilation then continues automatically into the generated image.

Do **not** jump directly to `main` from a cold machine. That bypasses `_start`,
including stack initialization, static relocation, and other runtime startup.

### Calling the compiler from another Dynphony program

`main` is a device-facing executable entry, not the reusable compiler API. The
core will expose an interface conceptually equivalent to:

```c
int dyn_compile_project(
    unsigned int project_address,
    unsigned int project_byte_length,
    unsigned int program_load_address,
    unsigned int output_address,
    unsigned int output_capacity
);
```

The first implementation can link this function and its caller into one image,
which lets the ordinary Dynphony ABI resolve it without a dynamic loader. A
resident monitor could later call a separately loaded compiler at a published
entry address, but that requires a stable binary ABI and symbol/export metadata
that do not exist yet.

### Loader matching the executable record

The proposed loader reads exactly the record above. With `START = 8192`, DCC1's
`program_load_address` must also have been 8192:

```text
pload size, [disk_pointer]       ; exact image byte length
add disk_pointer, 4
add end, size, START
mov pointer, START

copy:
    pload word, [disk_pointer]
    store_32 [pointer], word
    add pointer, 4
    add disk_pointer, 4
    cmp pointer, end
    jl copy

jmp START
```

The final copy may include up to three zero padding bytes. The loader must only
be entered after DCC1 reports success, and images are never empty.

Use `jmp START`, rather than `call START`, for an ordinary compiler output. Its
entry is `_start`: it initializes its own runtime and eventually enters its halt
loop, so it neither returns to the loader nor executes the loader's saved-register
epilogue. A later callable-image mode can define a returning entry point and may
then use `call`.

### Running a newly compiled program on the same computer

The simplest workflow is compile, copy the executable record's image from
persistent storage into program RAM, reset, and boot its `_start`. Running it without
resetting requires more machinery: the compiler must emit a relocatable/PIC
image into non-overlapping RAM, and a monitor must copy it from persistent
storage and transfer control while managing the stack and deciding whether the
child should return or halt. That monitor/callable-program ABI is a later
milestone and is not required for compiler self-hosting.

The Python packer and emulator driver only prepare and inspect persistent images;
they are not hidden dependencies of the compiler. A future editor or monitor on
Dynphony can write the same DCC1/DCP1 layout directly with `persistent_store`.
