#include <stdio.h>
#include <stdbool.h>

#define LIMBS 4


typedef struct {
    unsigned v[LIMBS];
} Big;


/* ============================================================
 * Basic bigint operations
 *
 * Little-endian limbs:
 *
 * v[0] = least significant 32 bits
 * v[7] = most significant 32 bits
 * ============================================================ */

void zero(Big *a) {
    for (int i = 0; i < LIMBS; i++)
        a->v[i] = 0;
}


void copy(Big *dst, Big *src) {
    for (int i = 0; i < LIMBS; i++)
        dst->v[i] = src->v[i];
}


void from_uint(Big *a, unsigned x) {
    zero(a);
    a->v[0] = x;
}


bool is_zero(Big *a) {
    for (int i = 0; i < LIMBS; i++) {
        if (a->v[i] != 0)
            return false;
    }

    return true;
}


bool is_one(Big *a) {
    if (a->v[0] != 1)
        return false;

    for (int i = 1; i < LIMBS; i++) {
        if (a->v[i] != 0)
            return false;
    }

    return true;
}


bool is_even(Big *a) {
    return (a->v[0] & 1) == 0;
}


/*
 * -1 if a < b
 *  0 if a == b
 * +1 if a > b
 */
int cmp(Big *a, Big *b) {
    for (int i = LIMBS - 1; i >= 0; i--) {
        if (a->v[i] < b->v[i])
            return -1;

        if (a->v[i] > b->v[i])
            return 1;
    }

    return 0;
}


/* ============================================================
 * a -= b
 *
 * Requires a >= b.
 * ============================================================ */

void sub(Big *a, Big *b) {
    unsigned borrow = 0;

    for (int i = 0; i < LIMBS; i++) {
        unsigned av = a->v[i];
        unsigned bv = b->v[i];

        unsigned x = av - bv;
        unsigned borrow1 = av < bv;

        unsigned y = x - borrow;
        unsigned borrow2 = x < borrow;

        a->v[i] = y;

        borrow = borrow1 | borrow2;
    }
}


/* ============================================================
 * a -= small unsigned
 * ============================================================ */

void sub_uint(Big *a, unsigned x) {
    unsigned old = a->v[0];

    a->v[0] = old - x;

    if (old >= x)
        return;

    for (int i = 1; i < LIMBS; i++) {
        old = a->v[i];

        a->v[i] = old - 1;

        if (old != 0)
            return;
    }
}


/* ============================================================
 * a += 2
 *
 * Used to walk through consecutive odd candidate numbers.
 * ============================================================ */

void add_two(Big *a) {
    unsigned old = a->v[0];

    a->v[0] = old + 2;

    /*
     * No carry.
     */
    if (a->v[0] >= old)
        return;

    for (int i = 1; i < LIMBS; i++) {
        old = a->v[i];

        a->v[i] = old + 1;

        if (a->v[i] != 0)
            return;
    }
}


/* ============================================================
 * Logical right shift by one bit
 * ============================================================ */

void shr1(Big *a) {
    unsigned carry = 0;

    for (int i = LIMBS - 1; i >= 0; i--) {
        unsigned next = a->v[i] & 1;

        a->v[i] =
            (a->v[i] >> 1) |
            (carry << 31);

        carry = next;
    }
}


/* ============================================================
 * Modular addition
 *
 * Computes:
 *
 *      a = (a + b) mod m
 *
 * Preconditions:
 *
 *      a < m
 *      b < m
 *
 *
 * We cannot simply add first, because a+b could overflow
 * 256 bits.
 *
 * Instead:
 *
 *      threshold = m - b
 *
 * If a >= threshold:
 *
 *      a+b >= m
 *
 * and therefore:
 *
 *      (a+b)-m = a-(m-b)
 *
 * Otherwise a+b < m, so the ordinary addition cannot overflow
 * 256 bits.
 * ============================================================ */

void addmod(Big *a, Big *b, Big *m) {
    Big threshold;

    copy(&threshold, m);
    sub(&threshold, b);

    if (cmp(a, &threshold) >= 0) {
        sub(a, &threshold);
        return;
    }

    unsigned carry = 0;

    for (int i = 0; i < LIMBS; i++) {
        unsigned av = a->v[i];
        unsigned bv = b->v[i];

        unsigned x = av + bv;
        unsigned carry1 = x < av;

        unsigned y = x + carry;
        unsigned carry2 = y < x;

        a->v[i] = y;

        carry = carry1 | carry2;
    }
}


/* ============================================================
 * Binary modular multiplication
 *
 * Computes:
 *
 *      out = (a * b) mod m
 *
 * But DOES NOT perform limb multiplication.
 *
 * It works directly on the full 256-bit bigint:
 *
 *      result = 0
 *
 *      while b:
 *          if b & 1:
 *              result += a mod m
 *
 *          b >>= 1
 *          a = 2*a mod m
 *
 * This maps very naturally onto Dynphony's shifts/add/sub.
 * ============================================================ */

void mulmod(Big *out, Big *aa, Big *bb, Big *m) {
    Big a;
    Big b;

    copy(&a, aa);
    copy(&b, bb);

    zero(out);

    while (!is_zero(&b)) {
        if (b.v[0] & 1)
            addmod(out, &a, m);

        shr1(&b);

        if (!is_zero(&b))
            addmod(&a, &a, m);
    }
}


/* ============================================================
 * Modular exponentiation
 *
 * out = base^exp mod m
 *
 * Binary square-and-multiply.
 * ============================================================ */

void powmod(Big *out, Big *base, Big *exp, Big *m) {
    Big x;
    Big e;
    Big tmp;

    copy(&x, base);
    copy(&e, exp);

    from_uint(out, 1);

    while (!is_zero(&e)) {
        if (e.v[0] & 1) {
            mulmod(&tmp, out, &x, m);
            copy(out, &tmp);
        }

        shr1(&e);

        if (!is_zero(&e)) {
            mulmod(&tmp, &x, &x, m);
            copy(&x, &tmp);
        }
    }
}


/* ============================================================
 * Big integer mod small integer
 *
 * Again no multiply/divide/modulo instruction needed.
 *
 * We process the number bit by bit:
 *
 *      r = (2*r + bit) mod d
 *
 * Since d is small, at each step:
 *
 *      r < d
 *
 * therefore:
 *
 *      2*r+1 < 2*d
 *
 * so at most one subtraction is needed.
 * ============================================================ */

unsigned mod_small(Big *a, unsigned d) {
    unsigned r = 0;

    for (int limb = LIMBS - 1; limb >= 0; limb--) {
        unsigned x = a->v[limb];

        for (int bit = 31; bit >= 0; bit--) {
            r <<= 1;

            if ((x >> bit) & 1)
                r++;

            if (r >= d)
                r -= d;
        }
    }

    return r;
}


/* ============================================================
 * One Miller-Rabin round
 * ============================================================ */

bool mr_round(Big *n, unsigned base) {
    Big nm1;
    Big d;
    Big a;
    Big x;
    Big tmp;

    /*
     * n - 1 = d * 2^s
     *
     * where d is odd.
     */

    copy(&nm1, n);
    sub_uint(&nm1, 1);

    copy(&d, &nm1);

    int s = 0;

    while (is_even(&d)) {
        shr1(&d);
        s++;
    }

    from_uint(&a, base);

    /*
     * x = base^d mod n
     */

    powmod(&x, &a, &d, n);

    if (is_one(&x))
        return true;

    if (cmp(&x, &nm1) == 0)
        return true;

    /*
     * x = x^(2^r)
     */

    for (int r = 1; r < s; r++) {
        mulmod(&tmp, &x, &x, n);
        copy(&x, &tmp);

        if (cmp(&x, &nm1) == 0)
            return true;

        /*
         * Reaching 1 before n-1 means composite.
         */
        if (is_one(&x))
            return false;
    }

    return false;
}


/* ============================================================
 * Probable-prime test
 * ============================================================ */

bool probable_prime(Big *n) {
    /*
     * Cheap filtering first.
     *
     * This saves a huge amount of Miller-Rabin work.
     */

    unsigned small_primes[] = {
        3,
        5,
        7,
        11,
        13,
        17,
        19,
        23,
        29,
        31,
        37,
        41,
        43,
        47,
        53,
        59,
        61,
        67,
        71,
        73,
        79,
        83,
        89,
        97,
        101,
        103,
        107,
        109,
        113,
        127,
        131,
        137,
        139,
        149,
        151,
        157,
        163,
        167,
        173,
        179,
        181,
        191,
        193,
        197,
        199,
        211,
        223,
        227,
        229,
        233,
        239,
        241,
        251
    };

    for (int i = 0; i < 53; i++) {
        if (mod_small(n, small_primes[i]) == 0)
            return false;
    }


    /*
     * Miller-Rabin bases.
     *
     * These make this a probable-prime test, not a proof.
     *
     * For benchmarking, 8 rounds is plenty.
     */

    unsigned bases[] = {
        2,
        3,
        5,
        7,
        11,
        13,
        17,
        19
    };

    for (int i = 0; i < 8; i++) {
        if (!mr_round(n, bases[i]))
            return false;
    }

    return true;
}


/* ============================================================
 * Deterministic pseudo-random generator
 *
 * xorshift32.
 *
 * Good for reproducible benchmarks.
 *
 * NOT cryptographically secure.
 * ============================================================ */

unsigned rng_state = 0x8a31f27d;


unsigned rng32(void) {
    unsigned x = rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    rng_state = x;

    return x;
}


/* ============================================================
 * Generate starting 256-bit odd candidate
 * ============================================================ */

void random_candidate(Big *a) {
    for (int i = 0; i < LIMBS; i++)
        a->v[i] = rng32();

    /*
     * Force bit 255:
     *
     * number is guaranteed to really be 256 bits.
     */
    a->v[LIMBS - 1] |= 0x80000000u;

    /*
     * Force odd.
     */
    a->v[0] |= 1;
}


/* ============================================================
 * Output
 *
 * DynCC currently doesn't support %08x, so print the 32-bit
 * limbs individually separated by spaces.
 *
 * Most-significant limb first.
 * ============================================================ */

void print_big(Big *a) {
    for (int i = LIMBS - 1; i >= 0; i--) {
        printf("%x", a->v[i]);

        if (i != 0)
            printf(" ");
    }

    printf("\n");
}


/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    Big candidate;

    random_candidate(&candidate);

    unsigned tested = 0;

    for (;;) {
        tested++;

        if (probable_prime(&candidate)) {
            printf("prime:\n");

            print_big(&candidate);

            printf("tested: %u\n", tested);

            return 0;
        }

        /*
         * Keep walking through odd integers:
         *
         * n, n+2, n+4, ...
         */
        add_two(&candidate);
    }

    return 0;
}