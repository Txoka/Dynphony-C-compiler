/* Read exactly 16 bytes, sort them, and output them in ascending order. */

#include <dynphony.h>

int main(void) {
    int index;
    char values[16];

    for (index = 0; index < 16; index++) {
        values[index] = (char)input();
    }

    for (index = 1; index < 16; index++) {
        char value = values[index];
        int position = index;

        while (position > 0 && values[position - 1] > value) {
            values[position] = values[position - 1];
            position -= 1;
        }
        values[position] = value;
    }

    for (index = 0; index < 16; index++) {
        output(values[index]);
    }

    return 0;
}
