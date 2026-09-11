"""Software arithmetic, compiled through the same frontend and backend as user C.

All loops have at most 32 iterations. Unsigned division retains the shifted-out
bit, so divisors >= 2**31 work without an unavailable 64-bit intermediate.
Division by zero is C undefined behavior; these helpers deterministically return 0.
"""

SOURCE = r"""
unsigned int __dyn_mul(unsigned int a, unsigned int b) {
    unsigned int r=0;
    while (b) { if (b & 1u) r += a; a <<= 1; b >>= 1; }
    return r;
}
unsigned int __dyn_udivmod(unsigned int a, unsigned int b, int remainder) {
    unsigned int q=0, r=0;
    int i;
    if (!b) return 0;
    for (i=0; i<32; i++) {
        unsigned int carry=r >> 31;
        r=(r << 1) | (a >> 31);
        a <<= 1; q <<= 1;
        if (carry || r >= b) { r -= b; q |= 1u; }
    }
    return remainder ? r : q;
}
unsigned int __dyn_udiv(unsigned int a, unsigned int b) { return __dyn_udivmod(a,b,0); }
unsigned int __dyn_umod(unsigned int a, unsigned int b) { return __dyn_udivmod(a,b,1); }
int __dyn_sdiv(int a, int b) {
    unsigned int ua=(unsigned int)a, ub=(unsigned int)b, q;
    if (a<0) ua=0u-ua;
    if (b<0) ub=0u-ub;
    q=__dyn_udiv(ua,ub);
    if ((a<0) != (b<0)) q=0u-q;
    return (int)q;
}
int __dyn_smod(int a, int b) {
    unsigned int ua=(unsigned int)a, ub=(unsigned int)b, r;
    if (a<0) ua=0u-ua;
    if (b<0) ub=0u-ub;
    r=__dyn_umod(ua,ub);
    if (a<0) r=0u-r;
    return (int)r;
}
"""
