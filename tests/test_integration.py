"""End-to-end compiler tests using complete, mixed-feature C programs.

These deliberately exercise the public ``compile_source`` API and execute the
raw binary in the reference Dynphony machine.  Unit-level feature tests remain
in test_compiler.py; these tests protect the boundaries between compiler stages.
"""

import unittest

from dynphony import Target, compile_source
from dynphony.emulator import Machine


def compile_and_run(source, expected, *, target=None, load_address=0, inputs=()):
    target = target or Target(load_address=load_address)
    result = compile_source(source, "integration.c", target)
    machine = Machine(result.image.binary, target.ram_size, load_address, inputs=inputs)
    halt = result.image.symbols["_halt"] + (load_address if target.pic else 0)
    actual = machine.run(halt)
    if actual != expected & 0xFFFFFFFF:
        raise AssertionError(f"expected {expected:#x}, got {actual:#x}")
    if machine.regs[14] != 0:
        raise AssertionError("program did not restore the stack pointer")
    return result, machine


MIXED_FEATURE_PROGRAM = r"""
enum { BIAS = 7 };

struct Totals {
    int sum;
    _Bool complete;
};

int values[] = {3, 5, 7};
int *start = values;

int finish(int value) {
    static int calls = 0;
    calls++;
    return value + BIAS + calls;
}

int collect(int count) {
    char odd[count];
    struct Totals totals = {0, 1};
    for (int i = 0; i < count; i++) {
        int value = start[i % 3];
        odd[i] = (char)(value & 1);
        totals.sum += value + odd[i];
    }
    if (!totals.complete) return 0;
    return finish(totals.sum);
}

int main(void) {
    int (*worker)(int) = collect;
    return worker(5);
}
"""


class CompilerIntegrationTests(unittest.TestCase):
    def test_mixed_language_features_at_fixed_and_pic_addresses(self):
        # values sum to 23; all are odd, so collect gives 28; finish adds 7 + 1.
        compile_and_run(MIXED_FEATURE_PROGRAM, 36)
        compile_and_run(MIXED_FEATURE_PROGRAM, 36, load_address=0x2400)
        compile_and_run(
            MIXED_FEATURE_PROGRAM,
            36,
            target=Target(pic=True),
            load_address=0x2403,
        )

    def test_printf_and_framebuffer_modes(self):
        source = r'''int main(void) {
            char *framebuffer = screen_framebuffer();
            unsigned int value = 0x2a;
            printf("value=%x", value);
            screen_cursor(0, 1);
            printf("%c", 'A');
            return framebuffer[0] + framebuffer[5] + framebuffer[96];
        }'''
        for include_framebuffer in (False, True):
            with self.subTest(include_framebuffer=include_framebuffer):
                result, machine = compile_and_run(
                    source,
                    ord("v") + ord("=") + ord("A"),
                    target=Target(include_framebuffer=include_framebuffer),
                )
                framebuffer = result.image.symbols["__dyn_printf_framebuffer"]
                self.assertEqual(machine.screen_updates, [(0, 0), (1, framebuffer)])
                self.assertEqual(
                    bytes(machine.memory[framebuffer : framebuffer + 8]), b"value=2a"
                )
                self.assertEqual(machine.memory[framebuffer + 96], ord("A"))
                if include_framebuffer:
                    self.assertLess(framebuffer, len(result.image.binary))
                else:
                    self.assertGreaterEqual(framebuffer, len(result.image.binary))

