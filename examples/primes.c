#include <stdio.h>
#include <stdbool.h>

#define N 8192

int main(void) {
    bool composite[N];

    for (int i = 0; i < N; i++)
        composite[i] = false;

    for (int p = 2; p * p < N; p++) {
        if (!composite[p]) {
            for (int x = p * p; x < N; x += p)
                composite[x] = true;
        }
    }

    for (int i = 2; i < N; i++) {
        if (!composite[i]) {
            printf("%d ", i);
        }
    }

    return 0;
}