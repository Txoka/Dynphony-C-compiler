#include <stdio.h>

int values[] = {3, 5, 7, 11};
int *start = values;

int factorial(int n) {
    if (n < 2) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    int sum = 0;
    for (int i = 0; i < 4; i++) sum += start[i];
    int res = factorial(5) + sum;
    printf("Result: %d", res);
    return res; /* 146 */
}
