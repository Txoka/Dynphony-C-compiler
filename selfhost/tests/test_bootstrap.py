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
        self.assertEqual(len(binary), 16)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 12), 52)

    def test_precedence_literals_unary_and_comments(self):
        source = "int main(void){/* fold */ return ~0 & (0x20 + 010 * 2); }"
        status, compiler, binary, control = run_stage0(
            self.compiler, source.encode("ascii")
        )
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 12), 48)

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
        self.assertEqual(program.run(control.program_load_address + 12), 77)

    def test_generated_image_honors_arbitrary_load_address(self):
        status, compiler, binary, control = run_stage0(
            self.compiler,
            b"int main(void){return 23;}",
            load_address=0x12340,
        )
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(control.program_load_address + 24), 23)

    def test_runtime_locals_assignment_control_flow_and_io(self):
        source = b"""int main(void) {
            int x = input();
            unsigned int total = 0;
            while (x > 0) {
                total = total + x;
                x = x - 1;
            }
            if (total > 10) output(total); else output(0);
            return total;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary, load_address=control.program_load_address, inputs=[5]
        )
        self.assertEqual(program.run(), 15)
        self.assertEqual(program.outputs, [15])

    def test_for_do_break_continue_and_increment(self):
        source = b"""int main(void) {
            int i = 0;
            int total = 0;
            for (i = 0; i < 6; i++) {
                if (i == 2) continue;
                if (i == 5) break;
                total += i;
            }
            do { total += 1; } while (total < 9);
            output(total);
            return total;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 9)
        self.assertEqual(program.outputs, [9])

    def test_runtime_software_multiply_divide_and_remainder(self):
        source = b"""int main(void) {
            unsigned int value = input();
            unsigned int product = value * 7;
            unsigned int quotient = product / 3;
            unsigned int remainder = product % 3;
            unsigned int result = quotient + remainder;
            output(result);
            return result;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary, load_address=control.program_load_address, inputs=[13]
        )
        self.assertEqual(program.run(max_steps=100_000), 31)
        self.assertEqual(program.outputs, [31])

    def test_scalar_declarators_casts_and_sizeof(self):
        source = b"""int main(void) {
            const char value = (char)input();
            unsigned long *pointer = 0;
            return sizeof(char) + sizeof(value) + sizeof(pointer)
                + (unsigned int)value;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary, load_address=control.program_load_address, inputs=[4]
        )
        self.assertEqual(program.run(), 10)

    def test_runtime_device_intrinsics(self):
        source = b"""int main(void) {
            unsigned int key = keyboard();
            persistent_store(4, key + time());
            screen(2, key);
            return persistent_load(4) ^ time_high();
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary,
            load_address=control.program_load_address,
            keyboard_inputs=[7],
            time_value=0x1122334400000005,
            persistent_size=256,
        )
        self.assertEqual(program.run(), 12 ^ 0x11223344)
        self.assertEqual(program.persistent_read(4), 12)
        self.assertEqual(program.screen_updates, [(2, 7)])

    def test_stack_backed_locals_and_width_correct_storage(self):
        source = b"""int main(void) {
            char a = 258;
            short b = 2;
            int c = 3;
            int d = 4;
            int e = 5;
            int f = 6;
            int g = 7;
            int h = 8;
            return a + b + c + d + e + f + g + h;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 37)

    def test_functions_parameters_nested_calls_and_recursion(self):
        source = b"""unsigned int factorial(unsigned int value) {
            if (value <= 1) return 1;
            return value * factorial(value - 1);
        }
        unsigned int combine(unsigned int a, unsigned int b) {
            return factorial(a) + factorial(b);
        }
        int main(void) {
            return combine(input(), 3);
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary, load_address=control.program_load_address, inputs=[5]
        )
        self.assertEqual(program.run(max_steps=1_000_000), 126)

    def test_postfix_increment_preserves_old_value(self):
        source = b"""int main(void) {
            int value = 3;
            int old = value++;
            return old * 10 + value;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 34)

    def test_forward_prototype_and_six_argument_abi(self):
        source = b"""int sum(int a, int b, int c, int d, int e, int f);
        int main(void) { return sum(1, 2, 3, 4, 5, 6); }
        int sum(int a, int b, int c, int d, int e, int f) {
            return a + b + c + d + e + f;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 21)

    def test_fixed_arrays_address_dereference_and_subscript(self):
        source = b"""int main(void) {
            int values[5];
            int index = 0;
            while (index < 5) {
                values[index] = index * index;
                index++;
            }
            int *pointer = &values[3];
            *pointer += 4;
            return values[3] + *pointer + values[4];
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(max_steps=500_000), 42)

    def test_nested_scope_shadowing(self):
        source = b"""int main(void) {
            int value = 2;
            { int value = 5; output(value); }
            output(value);
            return value;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 2)
        self.assertEqual(program.outputs, [5, 2])

    def test_global_scalars_arrays_and_static_data(self):
        source = b"""static int seed = 7;
        unsigned int values[4];
        int update(int index) {
            values[index] = seed + index;
            seed += 1;
            return values[index];
        }
        int main(void) {
            return update(2) + update(1) + values[2] + seed;
        }"""
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 36)

    def test_string_literal_pooling_escapes_and_concatenation(self):
        source = b'''int main(void) {
            char *text = "ab" "c\\n";
            return text[0] + text[1] + text[2] + text[3];
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 304)

    def test_static_pointer_relocations(self):
        source = b'''int value = 40;
        int *pointer = &value;
        char *text = "az";
        int main(void) { return *pointer + text[1]; }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 162)

    def test_static_array_initializer_data(self):
        source = b'''static int values[4] = {10, 20, 30};
        static char bytes[3] = {1, 2, 255};
        int main(void) {
            return values[0] + values[2] + values[3]
                + bytes[0] + bytes[1] + bytes[2];
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 298)

    def test_scaled_pointer_arithmetic(self):
        source = b'''int main(void) {
            int values[4];
            int *pointer = values;
            *(pointer + 2) = 40;
            pointer++;
            *(pointer + 2) = 2;
            return values[2] + values[3] + (*(2 + values) - 40);
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)


if __name__ == "__main__":
    unittest.main()
