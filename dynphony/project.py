"""DCP1 project bundles and DCC1 persistent compiler control records."""

from dataclasses import dataclass
from pathlib import Path, PurePosixPath


DCP_MAGIC = 0x44435031
DCC_MAGIC = 0x44434331
VERSION = 1
CONTROL_SIZE = 44
CONTROL_MODE_ADDRESS = 40
CONTROL_RESERVED_SIZE = CONTROL_SIZE
MODE_RUN_AFTER_COMPILE = 1
STATUS_PENDING = 0xFFFFFFFF
STATUS_RUNNING = 0xFFFFFFFE


class ProjectFormatError(ValueError):
    """Raised when a persistent compiler image or project is malformed."""


def _u32(value):
    if not 0 <= value <= 0xFFFFFFFF:
        raise ProjectFormatError(f"value does not fit u32: {value}")
    return value.to_bytes(4, "big")


def _align4(value):
    return (value + 3) & ~3


def _blob(value):
    value = bytes(value)
    return _u32(len(value)) + value + bytes((-len(value)) & 3)


def _path(name):
    name = PurePosixPath(name).as_posix()
    path = PurePosixPath(name)
    if (
        not name
        or path.is_absolute()
        or name == "."
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        raise ProjectFormatError(f"invalid project path: {name!r}")
    try:
        name.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ProjectFormatError(f"project path is not UTF-8: {name!r}") from exc
    return name


@dataclass(frozen=True)
class ProjectFile:
    path: str
    contents: bytes
    kind: int = 1

    def __post_init__(self):
        object.__setattr__(self, "path", _path(self.path))
        object.__setattr__(self, "contents", bytes(self.contents))
        if self.kind not in (1, 2):
            raise ProjectFormatError(f"unknown project file kind: {self.kind}")


@dataclass(frozen=True)
class Project:
    files: tuple[ProjectFile, ...]
    include_roots: tuple[str, ...] = ()
    definitions: tuple[str, ...] = ()

    def __post_init__(self):
        files = tuple(sorted(self.files, key=lambda item: item.path))
        if len({item.path for item in files}) != len(files):
            raise ProjectFormatError("duplicate project path")
        object.__setattr__(self, "files", files)
        object.__setattr__(
            self, "include_roots", tuple(_path(root) for root in self.include_roots)
        )
        object.__setattr__(self, "definitions", tuple(self.definitions))


@dataclass(frozen=True)
class Control:
    persistent_size: int
    project_address: int
    project_byte_length: int
    program_load_address: int
    output_address: int
    output_capacity: int
    status: int = STATUS_PENDING
    output_byte_length: int = 0
    mode: int = 0


def encode_project(project):
    out = bytearray()
    for value in (
        DCP_MAGIC, VERSION, 0, len(project.files), len(project.include_roots),
        len(project.definitions),
    ):
        out += _u32(value)
    for root in project.include_roots:
        out += _blob(root.encode("utf-8"))
    for definition in project.definitions:
        out += _blob(definition.encode("utf-8"))
    for item in project.files:
        out += _u32(item.kind)
        out += _blob(item.path.encode("utf-8"))
        out += _blob(item.contents)
    return bytes(out)


class _Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.position = 0

    def u32(self):
        if self.position + 4 > len(self.data):
            raise ProjectFormatError("truncated u32")
        value = int.from_bytes(self.data[self.position:self.position + 4], "big")
        self.position += 4
        return value

    def blob(self):
        length = self.u32()
        end = self.position + length
        padded = self.position + _align4(length)
        if end > len(self.data) or padded > len(self.data):
            raise ProjectFormatError("truncated blob")
        value = bytes(self.data[self.position:end])
        if any(self.data[end:padded]):
            raise ProjectFormatError("nonzero blob padding")
        self.position = padded
        return value


def decode_project(data):
    reader = _Reader(data)
    if reader.u32() != DCP_MAGIC:
        raise ProjectFormatError("invalid DCP1 magic")
    if reader.u32() != VERSION:
        raise ProjectFormatError("unsupported DCP1 version")
    if reader.u32() != 0:
        raise ProjectFormatError("unsupported DCP1 flags")
    file_count = reader.u32()
    root_count = reader.u32()
    definition_count = reader.u32()
    try:
        roots = tuple(reader.blob().decode("utf-8") for _ in range(root_count))
        definitions = tuple(
            reader.blob().decode("utf-8") for _ in range(definition_count)
        )
        files = []
        for _ in range(file_count):
            kind = reader.u32()
            path = reader.blob().decode("utf-8")
            files.append(ProjectFile(path, reader.blob(), kind))
        files = tuple(files)
    except UnicodeDecodeError as exc:
        raise ProjectFormatError("project text is not valid UTF-8") from exc
    if reader.position != len(reader.data):
        raise ProjectFormatError("trailing bytes after DCP1 project")
    project = Project(files, roots, definitions)
    if tuple(item.path for item in files) != tuple(item.path for item in project.files):
        raise ProjectFormatError("DCP1 files are not sorted")
    return project


def encode_control(control):
    values = (
        DCC_MAGIC, VERSION, control.persistent_size, control.project_address,
        control.project_byte_length, control.program_load_address,
        control.output_address, control.output_capacity, control.status,
        control.output_byte_length, control.mode,
    )
    return b"".join(_u32(value) for value in values)


def decode_control(data):
    if len(data) < CONTROL_SIZE:
        raise ProjectFormatError("truncated DCC1 control block")
    values = [int.from_bytes(data[i:i + 4], "big") for i in range(0, CONTROL_SIZE, 4)]
    if values[0] != DCC_MAGIC:
        raise ProjectFormatError("invalid DCC1 magic")
    if values[1] != VERSION:
        raise ProjectFormatError("unsupported DCC1 version")
    return Control(*values[2:])


def make_persistent_image(
    project, *, persistent_size, program_load_address=8192,
    project_address=64, output_address=None, run_after_compile=False,
):
    if persistent_size < 4 or persistent_size & (persistent_size - 1):
        raise ProjectFormatError("persistent size must be a power of two")
    payload = encode_project(project)
    output_address = output_address or _align4(project_address + len(payload))
    if project_address < CONTROL_RESERVED_SIZE or project_address & 3 or output_address & 3:
        raise ProjectFormatError("project and output addresses must be aligned")
    if project_address + len(payload) > output_address:
        raise ProjectFormatError("project overlaps output region")
    if output_address >= persistent_size:
        raise ProjectFormatError("persistent image has no output capacity")
    control = Control(
        persistent_size, project_address, len(payload), program_load_address,
        output_address, persistent_size - output_address,
        mode=MODE_RUN_AFTER_COMPILE if run_after_compile else 0,
    )
    image = bytearray(persistent_size)
    image[:CONTROL_SIZE] = encode_control(control)
    image[project_address:project_address + len(payload)] = payload
    return bytes(image)


def project_from_directory(
    root, *, include_roots=None, definitions=(), exclude=()
):
    root = Path(root).resolve()
    if not root.is_dir():
        raise ProjectFormatError(f"project directory not found: {root}")
    files = []
    excluded = tuple(_path(name).rstrip("/") for name in exclude)
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if any(relative == name or relative.startswith(name + "/") for name in excluded):
            continue
        if path.is_file() and path.suffix in (".c", ".h"):
            files.append(ProjectFile(relative, path.read_bytes(), 1 if path.suffix == ".c" else 2))
    roots = tuple(include_roots) if include_roots is not None else (("include",) if (root / "include").is_dir() else ())
    return Project(tuple(files), roots, tuple(definitions))


__all__ = [
    "CONTROL_SIZE", "CONTROL_MODE_ADDRESS", "CONTROL_RESERVED_SIZE",
    "MODE_RUN_AFTER_COMPILE", "Control", "DCC_MAGIC", "DCP_MAGIC", "Project",
    "ProjectFile", "ProjectFormatError", "STATUS_PENDING", "STATUS_RUNNING",
    "VERSION", "decode_control", "decode_project", "encode_control",
    "encode_project", "make_persistent_image", "project_from_directory",
]
