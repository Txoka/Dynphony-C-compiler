"""Built-in C declarations and names lowered directly to Dynphony opcodes."""

PROTOTYPES = r"""
unsigned int input(void);
void output(unsigned int value);
unsigned int keyboard(void);
void screen(unsigned int setting, unsigned int value);
unsigned int time(void);
unsigned int time_low(void);
unsigned int time_high(void);
unsigned int persistent_load(unsigned int address);
void persistent_store(unsigned int address, unsigned int value);
"""

NAMES = frozenset(
    {
        "input",
        "output",
        "keyboard",
        "screen",
        "time",
        "time_low",
        "time_high",
        "persistent_load",
        "persistent_store",
    }
)
