import unittest
from pathlib import Path

from dynphony.emulator import Machine, native_available, native_run

from selfhost.tools.bootstrap import build_stage0


def run_stage0(image, source):
    data = source.encode("ascii")
    machine = Machine(image.image.binary, inputs=[len(data), *data])
    halt = image.image.symbols["_halt"]
    if native_available():
        status = native_run(machine, halt, max_steps=50_000_000)
    else:
        status = machine.run(halt, max_steps=50_000_000)
    return status, machine


class BootstrapCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = build_stage0()

    def test_compiles_and_runs_example(self):
        source = Path("selfhost/examples/answer.c").read_text()
        status, compiler = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        self.assertEqual(len(compiler.outputs), 16)
        program = Machine(bytes(compiler.outputs))
        self.assertEqual(program.run(12), 52)

    def test_precedence_literals_unary_and_comments(self):
        source = "int main(void){/* fold */ return ~0 & (0x20 + 010 * 2); }"
        status, compiler = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(bytes(compiler.outputs))
        self.assertEqual(program.run(12), 48)

    def test_reports_parse_and_semantic_errors(self):
        for source, expected in (
            ("int main(void){return nope;}", 4),
            ("int main(void){return 1/0;}", 5),
        ):
            with self.subTest(source=source):
                status, compiler = run_stage0(self.compiler, source)
                self.assertEqual(status, expected)
                self.assertEqual(compiler.outputs, [])


if __name__ == "__main__":
    unittest.main()
