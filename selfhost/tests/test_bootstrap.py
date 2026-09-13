import unittest
from pathlib import Path

from dynphony.emulator import Machine, native_available
from dynphony.project import (
    Project,
    ProjectFile,
    decode_control,
    make_persistent_image,
    project_from_directory,
)

from selfhost.tools.bootstrap import build_stage0, run_machine, run_stage0


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
            ("enum Bad { SAME, SAME }; int main(void){return 0;}", 4),
            ("typedef int same; typedef char same; int main(void){return 0;}", 4),
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
        source = b"""unsigned int read_value(void) { return 12; }
        int main(void) {
            unsigned int key = keyboard();
            persistent_store(4, read_value());
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

    def test_void_function_bare_return(self):
        source = b'''int value;
        int pick(int input, int output) { return input + output; }
        void *identity(void *pointer) { return pointer; }
        void no_operation(void) {}
        void set_value(int next) {
            value = next;
            return;
        }
        int main(void) {
            no_operation();
            int next = pick(40, 2);
            int *pointer = identity(&next);
            set_value(*pointer);
            return value;
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_nested_division_spills_to_low_scratch_registers(self):
        source = b"int main(void) { return 1 + (82 / 2); }"
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_deep_subscript_scaling_spills_to_low_scratch_registers(self):
        source = b'''struct Triple { int a; int b; int c; };
        int main(void) {
            struct Triple values[2];
            values[1].c = 39;
            return 1 + (2 + values[1].c);
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

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

    def test_stack_passed_arguments(self):
        source = b'''int sum8(int a, int b, int c, int d,
            int e, int f, int g, int h) {
            return a + b + c + d + e + f + g + h;
        }
        int main(void) {
            return sum8(1, 2, 3, 4, 5, 6, 7, 8)
                + sum8(1, 1, 1, 1, 1, 1, 1, 1);
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 44)

    def test_project_links_multiple_translation_units(self):
        project = Project((
            ProjectFile("helper.c", b'''int add(int left, int right) {
                return left + right;
            }'''),
            ProjectFile("main.c", b'''int add(int left, int right);
            int main(void) { return add(19, 23); }'''),
        ))
        persistent = make_persistent_image(
            project, persistent_size=1 << 16, program_load_address=8192
        )
        machine = Machine(self.compiler.image.binary, persistent_size=1 << 16)
        machine.persistent[:] = persistent
        status = run_machine(
            machine, self.compiler.image.symbols["_halt"], 50_000_000
        )
        control = decode_control(machine.persistent)
        self.assertEqual(status, 0)
        self.assertEqual(control.status, 0)
        record = machine.persistent[
            control.output_address:
            control.output_address + control.output_byte_length
        ]
        image_length = int.from_bytes(record[:4], "big")
        binary = bytes(record[4:4 + image_length])
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_project_preprocesses_relative_and_rooted_includes(self):
        project = Project(
            (
                ProjectFile(
                    "include/constants.h",
                    b'''#ifndef CONSTANTS_H
                    #define CONSTANTS_H
                    #define BASE_VALUE 40
                    #endif''',
                    2,
                ),
                ProjectFile(
                    "src/local.h", b"int local_value(void);", 2
                ),
                ProjectFile(
                    "src/main.c",
                    b'''#include <constants.h>
                    #include <constants.h>
                    #include "local.h"
                    int local_value(void) { return PROJECT_OFFSET; }
                    int main(void) {
                        char *text = "BASE_VALUE";
                        return BASE_VALUE + local_value() + (text[0] == 'B');
                    }''',
                ),
            ),
            ("include",),
            ("PROJECT_OFFSET=1",),
        )
        persistent = make_persistent_image(
            project, persistent_size=1 << 16, program_load_address=8192
        )
        machine = Machine(self.compiler.image.binary, persistent_size=1 << 16)
        machine.persistent[:] = persistent
        status = run_machine(
            machine, self.compiler.image.symbols["_halt"], 50_000_000
        )
        control = decode_control(machine.persistent)
        self.assertEqual(status, 0)
        self.assertEqual(control.status, 0)
        record = machine.persistent[
            control.output_address:
            control.output_address + control.output_byte_length
        ]
        image_length = int.from_bytes(record[:4], "big")
        binary = bytes(record[4:4 + image_length])
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

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

    def test_sibling_blocks_may_reuse_local_names(self):
        source = b'''int main(void) {
            int total = 0;
            if (1) {
                int value = 19;
                total += value;
            }
            {
                int value = 23;
                total += value;
            }
            return total;
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_enum_definitions_and_constant_expressions(self):
        source = b'''enum Token {
            TOKEN_ZERO,
            TOKEN_START = 7,
            TOKEN_NEXT,
            TOKEN_MASK = (TOKEN_NEXT << 2) | 1,
        };
        int main(void) {
            int value = TOKEN_MASK;
            return TOKEN_ZERO + TOKEN_START + TOKEN_NEXT + value;
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 48)

    def test_scalar_typedefs_in_globals_parameters_and_locals(self):
        source = b'''typedef unsigned int word;
        typedef char byte;
        word base = 30;
        word add(byte left, word right) {
            word result = left + right;
            return result;
        }
        int main(void) { return add(12, base); }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_struct_layout_members_pointers_and_nesting(self):
        source = b'''struct Pair;
        typedef struct Pair *PairPointer;
        struct Pair {
            char tag;
            int value;
            short delta;
            PairPointer next;
        };
        struct Box {
            struct Pair pair;
            struct Pair *link;
            int values[3];
        };
        typedef struct Pair Pair;
        struct Pair global_pair;
        int main(void) {
            Pair local;
            Pair items[2];
            struct Box box;
            PairPointer pointer = &local;
            pointer->tag = 2;
            pointer->value = 30;
            local.delta = 4;
            box.pair.value = 5;
            box.link = pointer;
            box.values[1] = 1;
            global_pair.value = 7;
            local.next = &global_pair;
            pointer = items;
            pointer++;
            pointer->value = 9;
            return box.link->tag + box.link->value + local.delta
                + box.pair.value + box.values[1] + local.next->value
                + sizeof(struct Pair) + items[1].value;
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 74)

    def test_multidimensional_fixed_arrays_and_strides(self):
        source = b'''int global_values[2][3];
        struct Bytes { char values[2][3]; };
        int main(void) {
            int local_values[2][3];
            struct Bytes bytes;
            global_values[1][2] = 10;
            local_values[1][2] = 20;
            bytes.values[1][2] = 12;
            return global_values[1][2] + local_values[1][2]
                + bytes.values[1][2];
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    def test_runtime_multidimensional_vla(self):
        source = b'''int main(void) {
            unsigned int rows = input();
            unsigned int columns = input();
            int values[rows][columns];
            values[1][2] = 37;
            values[0][1] = 5;
            return values[1][2] + values[0][1];
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(
            binary, load_address=control.program_load_address, inputs=[2, 3]
        )
        self.assertEqual(program.run(), 42)

    def test_vla_sizeof_and_array_parameter_stride(self):
        source = b'''unsigned int row_size(
            unsigned int rows, unsigned int columns,
            int values[rows][columns]
        ) {
            return sizeof(*values);
        }
        int main(void) {
            unsigned int rows = 2;
            unsigned int columns = 3;
            int values[rows][columns];
            values[1][2] = 6;
            return sizeof(values) + row_size(rows, columns, values)
                + values[1][2];
        }'''
        status, compiler, binary, control = run_stage0(self.compiler, source)
        self.assertEqual(status, 0)
        program = Machine(binary, load_address=control.program_load_address)
        self.assertEqual(program.run(), 42)

    @unittest.skipUnless(native_available(), "requires native emulator")
    def test_compiler_reproduces_itself_byte_for_byte(self):
        persistent_size = 1 << 24
        load_address = 8192
        project = project_from_directory(
            "selfhost", exclude=("build", "examples", "tests")
        )
        persistent = make_persistent_image(
            project, persistent_size=persistent_size,
            program_load_address=load_address,
        )
        stage1 = Machine(
            self.compiler.image.binary, persistent_size=persistent_size
        )
        stage1.persistent[:] = persistent
        self.assertEqual(run_machine(
            stage1, self.compiler.image.symbols["_halt"], 900_000_000
        ), 0)
        control1 = decode_control(stage1.persistent)
        self.assertEqual(control1.status, 0)
        record1 = stage1.persistent[control1.output_address:]
        length1 = int.from_bytes(record1[:4], "big")
        stage2_binary = bytes(record1[4:4 + length1])

        stage2 = Machine(
            stage2_binary, load_address=load_address,
            persistent_size=persistent_size,
        )
        stage2.persistent[:] = persistent
        self.assertEqual(
            run_machine(stage2, load_address + 24, 1_000_000_000), 0
        )
        control2 = decode_control(stage2.persistent)
        self.assertEqual(control2.status, 0)
        record2 = stage2.persistent[control2.output_address:]
        length2 = int.from_bytes(record2[:4], "big")
        stage3_binary = bytes(record2[4:4 + length2])
        self.assertEqual(stage3_binary, stage2_binary)


if __name__ == "__main__":
    unittest.main()
