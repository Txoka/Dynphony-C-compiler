import json
import random
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from dynphony import CompileError, Target, compile_source
from dynphony.emulator import Machine, signed
from dynphony import isa

ROOT = Path(__file__).resolve().parents[1]


def run(source, expected=None, pic=False, address=0, ram=1 << 20, inputs=()):
    source = source.replace("-2147483648", "(-2147483647-1)")
    target = Target(ram_size=ram, pic=pic, load_address=0 if pic else address)
    result = compile_source(source, target=target)
    machine = Machine(result.image.binary, ram, address, inputs=inputs)
    halt = result.image.symbols["_halt"] + (address if pic else 0)
    actual = machine.run(halt)
    if expected is not None and actual != expected & 0xFFFFFFFF:
        raise AssertionError(
            f"expected {expected&0xffffffff:#x}, got {actual:#x}: {source}"
        )
    if machine.regs[14] != 0:
        raise AssertionError("stack pointer was not restored")
    return result, machine


class ExecutionTests(unittest.TestCase):
    def test_basic(self):
        run("int main(void) { int x=3; int y=4; return x+y; }", 7)

    def test_types_and_promotions(self):
        cases = [
            ("char c=255; return c;", 255),
            ("signed char c=255; return c;", -1),
            ("short c=65535; return c;", -1),
            ("unsigned short c=65535; return c+1;", 65536),
            ("unsigned char c=255; return c+c;", 510),
            ("signed char c=-128; return c >> 2;", -32),
            ("unsigned int x=0x80000000u; return x >> 31;", 1),
            ("long x=-2; return x;", -2),
            ("unsigned long x=0xffffffff; return x>0;", 1),
            ("int x=-1; unsigned int y=1; return x<y;", 0),
            ("int x=-7; return (char)x;", 249),
            ("unsigned int x=0xffffffff; return (short)x;", -1),
            ("char c=255; c++; return c;", 0),
            ("signed char c=127; c+=1; return c;", -128),
            ("int x=5; x<<=2; x^=3; x|=8; x&=31; x>>=1; return x;", 15),
        ]
        for body, expected in cases:
            with self.subTest(body=body):
                run("int main(void){" + body + "}", expected)

    def test_comparisons(self):
        for typ, a, b in [
            ("int", -1, 1),
            ("int", -2147483648, 2147483647),
            ("unsigned int", 4294967295, 1),
            ("int", 3, 3),
        ]:
            for op, expected in [
                ("==", a == b),
                ("!=", a != b),
                ("<", a < b),
                ("<=", a <= b),
                (">", a > b),
                (">=", a >= b),
            ]:
                with self.subTest(typ=typ, a=a, b=b, op=op):
                    run(
                        f"int main(void){{ {typ} a={str(a)+'u' if typ.startswith('unsigned') else a},b={str(b)+'u' if typ.startswith('unsigned') else b}; return a {op} b; }}",
                        int(expected),
                    )

    def test_control_flow(self):
        run(
            """int main(void) {
            int n=0,s=0;
            while(n<10) { n++; if(n==4) continue; if(n==8) break; s+=n; }
            for(int i=0;i<3;i++) { for(int j=0;j<2;j++) s++; }
            do { s++; } while(s<35);
            if(s==35) return s; else return -1;
        }""",
            35,
        )
        run("int main(void){int i=0; for(;;) { if(++i==5) break; } return i;}", 5)
        run("int main(void){int i=0; do {i++; continue;} while(i<3); return i;}", 3)

    def test_short_circuit_and_conditional(self):
        run(
            """int main(void){int x=0; int a=0 && ++x; int b=1 || ++x;
            int c=1 ? ++x : (x=100); int d=0 ? (x=100) : ++x;
            return x*100+a*10+b+c+d; }""",
            204,
        )
        run("int main(void){int a=0,b=0; return (a=3,b=a+4,b);}", 7)
        run("int main(void){int *p=0; return p && *p;}", 0)

    def test_recursion_and_nested_calls(self):
        run(
            """int add(int a,int b){return a+b;}
            int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }
            int main(void){return add(fib(8),add(10,11));}""",
            42,
        )
        run(
            """int six(int a,int b,int c,int d,int e,int f) {return a+2*b+3*c+4*d+5*e+6*f;}
            int id(int x){return x;}
            int main(void){return six(id(1),id(2),id(3),id(4),id(5),id(6));}""",
            91,
        )

    def test_arrays_and_pointers(self):
        run(
            """int main(void){int a[4]={2,4}; int *p=a; p++; *p+=3;
            return a[0]+p[0]+a[2]+a[3]+(p-a); }""",
            10,
        )
        run(
            """int sum(int a[],int n){int s=0; for(int i=0;i<n;i++) s+=a[i]; return s;}
            int main(void){int a[]={2,3,4}; return sum(a,3);}""",
            9,
        )
        run(
            "int main(void){int a[2][3]={{1,2},{3,4,5}}; return a[0][2]+a[1][2]+sizeof(a);}",
            29,
        )
        run("int main(void){int a=4; int *p=&a; int **q=&p; **q=9; return a;}", 9)
        run("int main(void){int a[4]={0}; int *p=a+3; p-=2; return p-a;}", 1)
        run("int main(void){int a[2]={3,4}; int *p=a; *p+++=2; return a[0]*10+*p;}", 54)
        run('int main(void){char s[6]="Hi"; return s[0]+s[1]+s[2]+s[5];}', 177)
        run("int main(void){int a=0; return sizeof(a++)+a;}", 4)

    def test_big_endian_and_unaligned(self):
        run(
            "int main(void){int x=0x12345678; unsigned char *p=(unsigned char*)&x; return p[0]*256+p[3];}",
            0x1278,
        )
        run(
            "int main(void){char a[6]={0}; int *p=(int*)(a+1); *p=0x12345678; return a[1]+a[4];}",
            0x8A,
        )

    def test_globals_strings_and_relocations(self):
        source = """static int a[3]={10,20,30}; int z[10]; int *p=&a[1];
            char text[]="abc"; char *s="de";
            int f(int x){return x+1;} int (*fn)(int)=f;
            int main(void){z[9]=4; return *p+text[2]+s[1]+z[9]+fn(5);}"""
        for pic, address in [(False, 0), (False, 0x12340), (True, 0), (True, 0x23451)]:
            with self.subTest(pic=pic, address=address):
                run(source, 230, pic, address)

    def test_pic_binary_identical_and_reentry(self):
        source = "int x=13; int *p=&x; int main(void){return *p;}"
        a = compile_source(source, target=Target(pic=True))
        b = compile_source(source, target=Target(pic=True, load_address=0x10000))
        self.assertEqual(a.image.binary, b.image.binary)
        for base in (0, 128, 0x10003, 0x21000):
            m = Machine(a.image.binary, 1 << 20, base)
            self.assertEqual(m.run(base + a.image.symbols["_halt"]), 13)
            m.pc = base
            self.assertEqual(m.run(base + a.image.symbols["_halt"]), 13)

    def test_static_constant_conversions(self):
        run("int x=(signed char)255; int main(void){return x;}", -1)
        run("unsigned int x=(0xffffffffu+1u)/2u; int main(void){return x;}", 0)
        run("int x=-7/3; int y=-7%3; int main(void){return x*10+y;}", -21)
        run("int x=1 ? 7 : 1/0; int y=0 && 1/0; int main(void){return x+y;}", 7)
        run("int a[2+3]={1,2}; int main(void){return sizeof(a)+a[4];}", 20)

    def test_source_diagnostics_for_unsupported_semantics(self):
        for source in [
            "int f(int x){int x=3;return x;} int main(void){return f(1);}",
            "const int x=3; int main(void){return x;}",
            "int main(void){volatile int x=3;return x;}",
        ]:
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source)

    def test_function_pointers(self):
        run(
            """int inc(int x){return x+1;} int apply(int (*fn)(int),int x){return (*fn)(x);}
            int main(void){int (*p)(int)=inc; return apply(p,41);}""",
            42,
            True,
            0x10100,
        )

    def test_scope(self):
        run(
            "int x=2; int main(void){int x=3; {int x=4; x++;} for(int x=0;x<3;x++){} return x;}",
            3,
        )
        run(
            "typedef unsigned short word; word x=65535; int main(void){return x+1;}",
            65536,
        )

    def test_void(self):
        run(
            "int x; void f(int a){x=a; return;} int main(void){f(9); (void)x; return x;}",
            9,
        )

    def test_callee_saved_registers(self):
        result = compile_source(
            "int f(int n){if(n<2)return 1;return n*f(n-1);} int main(void){return f(6);}"
        )
        m = Machine(result.image.binary, 1 << 20)
        for r in range(8, 14):
            m.regs[r] = r * 19
        self.assertEqual(m.run(result.image.symbols["_halt"]), 720)
        for r in range(8, 14):
            self.assertEqual(m.regs[r], r * 19)
        self.assertEqual(m.regs[14], 0)

    def test_far_code_and_data(self):
        run("char pad[70000]; int value=37; int main(void){return value;}", 37)
        run("int main(void){return 0x12345678;}", 0x12345678, False, 0x23456)

    def test_software_arithmetic(self):
        rng = random.Random(917)
        for unsigned in (False, True):
            pairs = [(0, 1), (1, 1), (37, 5), (0x7FFFFFFF, 17)]
            pairs += (
                [
                    (0xFFFFFFFF, 0x80000000),
                    (0x80000000, 0xFFFFFFFF),
                    (0xFFFFFFFF, 0xFFFFFFFF),
                ]
                if unsigned
                else [
                    (-7, 3),
                    (7, -3),
                    (-7, -3),
                    (-2147483648, 2),
                    (2147483647, -2147483648),
                ]
            )
            pairs += [
                (
                    (
                        rng.randrange(0, 2**32)
                        if unsigned
                        else rng.randrange(-(2**31), 2**31)
                    ),
                    (
                        rng.randrange(1, 2**32)
                        if unsigned
                        else rng.choice([-1, 1]) * rng.randrange(1, 2**31)
                    ),
                )
                for _ in range(12)
            ]
            for a, b in pairs:
                q = (
                    a // b
                    if unsigned
                    else (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
                )
                for op, expected in [("*", a * b), ("/", q), ("%", a - q * b)]:
                    with self.subTest(unsigned=unsigned, a=a, b=b, op=op):
                        typ = "unsigned int" if unsigned else "int"
                        run(
                            f"int main(void){{{typ} a=({typ})input(),b=({typ})input();return a{op}b;}}",
                            expected,
                            inputs=(a & 0xFFFFFFFF, b & 0xFFFFFFFF),
                        )

    def test_c_escapes_and_adjacent_strings(self):
        run(
            r"""int main(void){char *s="a\?\n" "\x41\101"; return s[0]+s[1]+s[2]+s[3]+s[4]+s[5];}""",
            300,
        )
        run(r"""int main(void){return '\?'+'\n'+'\101';}""", 138)
        with self.assertRaises(CompileError):
            compile_source("int main(void){return 2147483648;}")
        with self.assertRaises(CompileError):
            compile_source("int main(void){unsigned signed int x; return 0;}")

    def test_comments_and_literals(self):
        run("/*head*/int main(void){// line\n return 010 + 0x10 + 'A';}", 89)
        run('int main(void){char *s="/*not a comment*/"; return s[0];}', 47)


class DiagnosticTests(unittest.TestCase):
    def test_rejections(self):
        cases = [
            ("#define X 1\nint main(void){return X;}", "preprocessor"),
            ("int main(void){return missing;}", "undeclared"),
            ("int main(void){float x=1.0;return 0;}", "only integer"),
            ("int main(void){long long x;return 0;}", "64-bit"),
            ("int f(int a); int main(void){return f(1);}", "undefined symbol"),
            ("int main(void){int a[2]; a=0;return 0;}", "lvalue"),
            ("int main(void){break;return 0;}", "outside loop"),
            ("int f(int x){return x;} int main(void){return f();}", "arguments"),
            ("int main(void){int n=2; int a[n];return 0;}", "constant"),
            ("int main(void){static int x;return 0;}", "storage"),
            (
                "int f(int a,int b,int c,int d,int e,int f,int g){return a;} int main(void){return 0;}",
                "six",
            ),
            ("int main(void){int *p=2;return 0;}", "convert"),
            ("int main(void){int a[1]={1,2};return 0;}", "too many"),
            ("void main(void){}", "entry point"),
            ("int main(void){return __dyn_mul(1,2);}", "reserved"),
            ("int main(void){return 1.25;}", "floating"),
            ("struct X{int a;}; int main(void){return 0;}", "unsupported"),
            ("int main(void){switch(1){case 1:return 1;}return 0;}", "unsupported"),
        ]
        for source, message in cases:
            with self.subTest(source=source):
                with self.assertRaisesRegex(CompileError, message):
                    compile_source(source)

    def test_target_validation(self):
        for target in [
            Target(ram_size=12345),
            Target(persistent_size=13),
            Target(load_address=-1),
            Target(ram_size=4),
        ]:
            with self.subTest(target=target), self.assertRaises(CompileError):
                compile_source("int main(void){return 0;}", target=target)


class EncodingTests(unittest.TestCase):
    def test_towers_of_hanoi_example(self):
        source = (ROOT / "examples/towers_of_hanoi.c").read_text()
        result = compile_source(source)
        machine = Machine(result.image.binary, inputs=[2, 0, 2, 1])
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 0)
        moves = [(0, 2), (0, 1), (2, 1), (0, 2), (1, 0), (1, 2), (0, 2)]
        expected = []
        for source_location, destination_location in moves:
            expected.extend([source_location, 5, destination_location, 5])
        self.assertEqual(machine.outputs, expected)
        self.assertNotIn("move_one", result.image.symbols)
        self.assertIn("cbranch", result.ir.dump())
        self.assertIn("tailcall", result.ir.dump())
        self.assertNotIn("main", result.image.symbols)
        self.assertEqual(result.image.frames["_start"], 4)
        self.assertLessEqual(len(result.image.binary), 348)

    def test_scalar_locals_fold_to_constant_return(self):
        result = compile_source(
            "int main(void){int a=12345; int b=6789; return a+b;}"
        )
        self.assertNotIn("main", result.image.symbols)
        self.assertEqual(result.image.frames["_start"], 4)
        halt = len(isa.cheap_constant(1, 19134))
        self.assertEqual(result.image.symbols["_halt"], halt)
        self.assertEqual(result.image.binary[:halt], isa.cheap_constant(1, 19134))
        machine = Machine(result.image.binary, 256)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 19134)
        self.assertEqual(machine.steps, 1)

    def test_readonly_parameters_stay_in_input_registers(self):
        result = compile_source(
            "int add(int a,int b){return a+b;} int (*keep)(int,int)=add; int main(void){return add(20,22);}"
        )
        address = result.image.symbols["add"]
        self.assertEqual(result.image.frames["add"], 4)
        self.assertEqual(
            result.image.binary[address : address + 3], isa.alu("add", 1, 1, 2)
        )
        machine = Machine(result.image.binary, 1024)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 42)

    def test_address_taken_parameter_uses_safe_stack_path(self):
        result = compile_source(
            "int f(int a){int *p=&a; *p+=1; return a;} "
            "int (*keep)(int)=f; int main(void){return f(41);}"
        )
        self.assertGreater(result.image.frames["f"], 4)
        machine = Machine(result.image.binary, 2048)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 42)

    def test_optimized_leaf_and_halt(self):
        result = compile_source("int main(void){return 42;}")
        binary = result.image.binary
        halt = result.image.symbols["_halt"]
        self.assertNotIn("main", result.image.symbols)
        self.assertEqual(result.image.frames["_start"], 4)
        self.assertEqual(binary[:halt], isa.cheap_constant(1, 42))
        self.assertEqual(binary[halt : halt + 4], isa.jump("jmp", halt, True))
        self.assertNotIn("__dyn_mul", result.image.symbols)
        self.assertNotIn("__dyn_udiv", result.image.symbols)

        machine = Machine(binary, 256)
        self.assertEqual(machine.run(halt), 42)
        machine.step()
        self.assertEqual(machine.pc, halt)

    def test_global_fixed_point_prunes_branch_and_relocates_single_caller(self):
        result = compile_source(
            "int dead(void){return 99;} "
            "int add3(int x){return x+3;} "
            "int main(void){if(0)return dead(); return add3(5);}"
        )
        self.assertEqual(set(result.image.symbols), {"_start", "_halt"})
        self.assertNotIn("function dead", result.ir.dump())
        self.assertNotIn("function add3", result.ir.dump())
        self.assertNotIn("function main", result.ir.dump())
        machine = Machine(result.image.binary, 256)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 8)

    def test_tier_one_algebraic_identities_reach_fixed_point(self):
        result = compile_source(
            "int main(void){int x=37; return (((x+0)|0)^0) + (x-x) + (x&0);}"
        )
        self.assertEqual(set(result.image.symbols), {"_start", "_halt"})
        self.assertNotIn("binary", result.ir.dump())
        machine = Machine(result.image.binary, 256)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 37)

    def test_pic_halt_is_one_repeated_register_jump(self):
        result = compile_source("int main(void){return 42;}", target=Target(pic=True))
        halt = result.image.symbols["_halt"]
        self.assertEqual(result.image.binary[halt : halt + 3], isa.jump("jmp", 7))
        machine = Machine(result.image.binary, 1024, 0x80)
        runtime_halt = 0x80 + halt
        self.assertEqual(machine.run(runtime_halt), 42)
        machine.step()
        self.assertEqual(machine.pc, runtime_halt)

    def test_power_of_two_strength_reduction_prunes_multiply(self):
        result = compile_source(
            "int main(void){int a[4]={1,2,3,4}; int i=3; return a[i];}"
        )
        self.assertNotIn("__dyn_mul", result.image.symbols)
        machine = Machine(result.image.binary, 4096)
        self.assertEqual(machine.run(result.image.symbols["_halt"]), 4)

    def test_golden_bytes(self):
        self.assertEqual(isa.alu("add", 1, 2, 3), bytes.fromhex("24 12 03"))
        self.assertEqual(isa.alu("sub", 14, 14, 4, True), bytes.fromhex("35 ee 00 04"))
        self.assertEqual(isa.alu("cmp", 15, 1, 2), bytes.fromhex("2a f1 02"))
        self.assertEqual(isa.counter(13), bytes.fromhex("07 d0"))
        self.assertEqual(isa.mov(3, 4), bytes.fromhex("21 30 04"))
        self.assertEqual(isa.mov(3, 0x1234, True), bytes.fromhex("31 30 12 34"))
        self.assertEqual(isa.load(2, 1, 2), bytes.fromhex("61 10 02"))
        self.assertEqual(isa.store(4, 2, 1), bytes.fromhex("66 01 02"))
        self.assertEqual(isa.jump("jl", 7), bytes.fromhex("44 0f 07"))
        self.assertEqual(
            isa.call(7),
            bytes.fromhex("07 f0 34 ff 00 10 35 ee 00 04 66 0f 0e 48 0f 07"),
        )
        self.assertEqual(
            isa.call(0x1234, True),
            bytes.fromhex(
                "07 f0 34 ff 00 11 35 ee 00 04 66 0f 0e 58 0f 12 34"
            ),
        )
        self.assertEqual(isa.ret(), bytes.fromhex("62 f0 0e 34 ee 00 04 48 0f 0f"))

    def test_io_and_persistent_intrinsics(self):
        source = """int main(void) {
            unsigned int a = input();
            unsigned int k = keyboard();
            output(a + k);
            screen(2, a);
            persistent_store(4, a + k);
            return time() ^ time_high() ^ persistent_load(4);
        }"""
        result = compile_source(source, target=Target(ram_size=4096, persistent_size=256))
        self.assertNotIn("call", "\n".join(
            line for line in result.ir.dump().splitlines() if "__dyn_" not in line
        ))
        machine = Machine(
            result.image.binary,
            4096,
            inputs=[7],
            keyboard_inputs=[5],
            time_value=0x1122334455667788,
            persistent_size=256,
        )
        actual = machine.run(result.image.symbols["_halt"])
        self.assertEqual(machine.outputs, [12])
        self.assertEqual(machine.screen_updates, [(2, 7)])
        self.assertEqual(machine.persistent_read(4), 12)
        self.assertEqual(actual, 0x55667788 ^ 0x11223344 ^ 12)

    def test_intrinsic_immediate_encodings_and_reserved_names(self):
        result = compile_source(
            "int main(void){output(0x1234); screen(3, 0xabcd); return 0;}"
        )
        self.assertIn(isa.output(0x1234, True), result.image.binary)
        self.assertIn(isa.screen(1, 0xABCD, True), result.image.binary)
        with self.assertRaisesRegex(CompileError, "reserved Dynphony intrinsic"):
            compile_source("unsigned int input(void){return 1;} int main(void){return 0;}")

    def test_encoders_against_supplied_spec(self):
        lines = (ROOT / "docs/isa.txt").read_text().splitlines()
        definitions = {
            line: lines[i + 1]
            for i, line in enumerate(lines[:-1])
            if lines[i + 1] and set(lines[i + 1].replace(" ", "")) <= set("01abcdv")
        }

        def encode(signature, **fields):
            bits = definitions[signature].replace(" ", "")
            for key, value in fields.items():
                digits = iter(f"{value:0{bits.count(key)}b}")
                bits = "".join(next(digits) if c == key else c for c in bits)
            return int(bits, 2).to_bytes(len(bits) // 8, "big")

        for op in isa.ALU:
            if op == "cmp":
                continue
            for immediate in (False, True):
                suffix = "%c:U16(immediate | label)" if immediate else "%c(register)"
                signature = f"{op} %a(register), %b(register), {suffix}"
                value = 0x1234 if immediate else 9
                self.assertEqual(
                    isa.alu(op, 3, 5, value, immediate),
                    encode(signature, a=3, b=5, c=value),
                )
        for op in isa.JUMP:
            self.assertEqual(isa.jump(op, 6), encode(f"{op} %a(register)", a=6))
        self.assertEqual(isa.call(6), encode("call %a(register)", a=6))
        self.assertEqual(
            isa.call(0x1234, True),
            encode("call %a:U16(immediate | label)", a=0x1234),
        )
        self.assertEqual(isa.ret(), encode("ret"))
        self.assertEqual(isa.mov(3, 4), encode("mov %a(register), %b(register)", a=3, b=4))
        self.assertEqual(isa.mov(3, 0x1234, True), encode("mov %a(register), %b:U16(immediate | label)", a=3, b=0x1234))
        self.assertEqual(isa.input_(3), encode("in %a(register)", a=3))
        self.assertEqual(isa.output(4), encode("out %b(register)", b=4))
        self.assertEqual(isa.output(0x1234, True), encode("out %a:U16(immediate)", a=0x1234))
        self.assertEqual(isa.keyboard(5), encode("keyboard %a(register)", a=5))
        self.assertEqual(isa.screen(2, 6), encode("screen %a(register), %b(register)", a=2, b=6))
        self.assertEqual(isa.screen(2, 0x1234, True), encode("screen %a(register), %b:U16(immediate)", a=2, b=0x1234))
        self.assertEqual(isa.time(0, 7), encode("time_0 %a(register)", a=7))
        self.assertEqual(isa.time(1, 8), encode("time_1 %a(register)", a=8))
        self.assertEqual(isa.persistent_load(3, 4), encode("pload %dest(register), [%adr(register)]", d=3, a=4))
        self.assertEqual(isa.persistent_store(4, 3), encode("pstore [%adr(register)], %value(register)", a=4, v=3))

    def test_machine_memory_and_zero_register(self):
        m = Machine(isa.constant(0, 123) + isa.constant(1, 0xFFFFFFFF), 256)
        m.write(255, 0x12345678, 4)
        self.assertEqual(m.read(255, 4), 0x12345678)
        m = Machine(isa.constant(0, 123), 256)
        for _ in range(3):
            m.step()
        self.assertEqual(m.regs[0], 0)

    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            cmd = [
                sys.executable,
                "-m",
                "dynphony",
                str(ROOT / "examples/demo.c"),
                "-o",
                str(path / "demo.bin"),
                "--pic",
                "--run",
                "--run-address",
                "0x12345",
                "--map",
                str(path / "map.json"),
                "--emit-ir",
                str(path / "demo.ir"),
            ]
            result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("returned 146", result.stdout)
            self.assertTrue((path / "demo.bin").stat().st_size > 0)
            self.assertTrue(
                json.loads((path / "map.json").read_text())["target"]["pic"]
            )
            self.assertIn("function _start", (path / "demo.ir").read_text())


if __name__ == "__main__":
    unittest.main()
