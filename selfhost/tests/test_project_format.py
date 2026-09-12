import tempfile
import unittest
from pathlib import Path

from dynphony.project import (
    CONTROL_SIZE,
    Project,
    ProjectFile,
    ProjectFormatError,
    decode_control,
    decode_project,
    encode_project,
    make_persistent_image,
    project_from_directory,
)
from dynphony.cli import main as cli_main


class ProjectFormatTests(unittest.TestCase):
    def test_project_round_trip_is_sorted_and_binary_safe(self):
        project = Project(
            (
                ProjectFile("src/z.c", b"int z;\x00"),
                ProjectFile("include/a.h", b"#pragma once\n", 2),
            ),
            ("include",),
            ("VALUE=7",),
        )
        encoded = encode_project(project)
        self.assertEqual(decode_project(encoded), project)
        self.assertEqual(decode_project(encoded).files[0].path, "include/a.h")

    def test_persistent_image_contains_control_and_project(self):
        project = Project((ProjectFile("main.c", b"int main(void){return 0;}"),))
        image = make_persistent_image(
            project, persistent_size=4096, program_load_address=8192
        )
        control = decode_control(image[:CONTROL_SIZE])
        self.assertEqual(control.persistent_size, 4096)
        self.assertEqual(control.program_load_address, 8192)
        self.assertEqual(
            decode_project(
                image[
                    control.project_address:
                    control.project_address + control.project_byte_length
                ]
            ),
            project,
        )
        self.assertEqual(control.output_capacity, 4096 - control.output_address)

    def test_rejects_malformed_project(self):
        with self.assertRaises(ProjectFormatError):
            ProjectFile("../escape.c", b"")
        good = encode_project(Project((ProjectFile("a.c", b"x"),)))
        with self.assertRaises(ProjectFormatError):
            decode_project(good[:-1])

    def test_directory_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "include").mkdir()
            (root / "include/x.h").write_text("#define X 1\n")
            (root / "main.c").write_text("int main(void){return X;}\n")
            (root / "ignored.txt").write_text("no")
            project = project_from_directory(root, exclude=("ignored",))
        self.assertEqual(project.include_roots, ("include",))
        self.assertEqual([f.path for f in project.files], ["include/x.h", "main.c"])
        self.assertEqual([f.kind for f in project.files], [2, 1])

    def test_cli_loads_and_saves_persistent_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.c"
            initial = root / "initial.bin"
            saved = root / "saved.bin"
            source.write_text(
                "#include <dynphony.h>\n"
                "int main(void){persistent_store(4,0x12345678u);return 0;}\n"
            )
            initial.write_bytes(bytes(256))
            status = cli_main([
                str(source), "-o", str(root / "program.bin"), "--run",
                "--persistent-size", "256", "--persistent-load", str(initial),
                "--persistent-save", str(saved),
            ])
            self.assertEqual(status, 0)
            self.assertEqual(saved.read_bytes()[4:8], bytes.fromhex("12345678"))


if __name__ == "__main__":
    unittest.main()
