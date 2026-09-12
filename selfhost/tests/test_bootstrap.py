import unittest
from pathlib import Path

from dynphony.emulator import Machine

from selfhost.tools.bootstrap import build_stage0, run_stage0


class BootstrapCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = build_stage0()

    def test_compiles_and_runs_example(self):
        source = Path("selfhost/examples/answer.c").read_text()
        status, compiler, binary, control = run_stage0(
            self.compiler, source.encode("ascii")
        )
        self.assertEqual(status, 0)
        self.assertEqual(len(binary), 27)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 24), 52)

    def test_precedence_literals_unary_and_comments(self):
        source = "int main(void){/* fold */ return ~0 & (0x20 + 010 * 2); }"
        status, compiler, binary, control = run_stage0(
            self.compiler, source.encode("ascii")
        )
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 24), 48)

    def test_reports_parse_and_semantic_errors(self):
        for source, expected in (
            ("int main(void){return nope;}", 4),
            ("int main(void){return 1/0;}", 5),
        ):
            with self.subTest(source=source):
                status, compiler, binary, control = run_stage0(
                    self.compiler, source.encode("ascii")
                )
                self.assertEqual(status, expected)
                self.assertEqual(binary, b"")
                self.assertEqual(control.output_byte_length, 8)

    def test_character_comparison_logical_conditional_and_comma(self):
        source = r"""int main(void) {
            return (0 && (1 / 0)), (1 || (1 / 0))
                ? ('A' == 65 && 9 >= 8 && !(3 != 3) ? 77u : 2)
                : 1;
        }"""
        status, compiler, binary, control = run_stage0(
            self.compiler, source.encode("ascii")
        )
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 24), 77)

    def test_generated_image_honors_arbitrary_load_address(self):
        status, compiler, binary, control = run_stage0(
            self.compiler,
            b"int main(void){return 23;}",
            load_address=0x12340,
        )
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 24), 23)


if __name__ == "__main__":
    unittest.main()
