int foo(int x);

int main(void) {
    int a = 4;
    return foo(a);
}

int foo(int x) {
    int b = 34;
    int c = 43;
    return x + b * c;
}
