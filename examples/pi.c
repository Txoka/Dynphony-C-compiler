#include <stdio.h>

const unsigned int N = 402;


/*
 * 402 x 32-bit words.
 *
 * Big-endian logical representation:
 *
 *   a[0]     = integer word
 *   a[1]     = most significant fractional word
 *   ...
 *   a[N - 1] = least significant fractional word
 *
 * Fractional precision:
 *
 *   401 * 32 = 12832 bits
 *
 * ~= 3863 decimal fractional digits.
 *
 * We print 3838, leaving ~25 guard digits.
 */


/* --------------------------------------------------------- */
/* Basic operations                                          */
/* --------------------------------------------------------- */

void zero(unsigned int *a)
{
    unsigned int i;

    i = 0;

    while (i < N) {
        a[i] = 0;
        i = i + 1;
    }
}


void copy(unsigned int *a, unsigned int *b)
{
    unsigned int i;

    i = 0;

    while (i < N) {
        a[i] = b[i];
        i = i + 1;
    }
}


unsigned int nonzero(unsigned int *a)
{
    unsigned int i;

    i = 0;

    while (i < N) {
        if (a[i] != 0)
            return 1;

        i = i + 1;
    }

    return 0;
}


void set_one(unsigned int *a)
{
    zero(a);
    a[0] = 1;
}


/* --------------------------------------------------------- */
/* Addition                                                  */
/* --------------------------------------------------------- */

void add(unsigned int *a, unsigned int *b)
{
    unsigned int i;
    unsigned int x;
    unsigned int y;
    unsigned int carry;
    unsigned int carry1;
    unsigned int carry2;

    i = N;
    carry = 0;

    while (i != 0) {
        i = i - 1;

        x = a[i] + b[i];

        if (x < a[i])
            carry1 = 1;
        else
            carry1 = 0;

        y = x + carry;

        if (y < x)
            carry2 = 1;
        else
            carry2 = 0;

        a[i] = y;

        carry = carry1 | carry2;
    }
}


/* --------------------------------------------------------- */
/* Subtraction                                               */
/* --------------------------------------------------------- */

void sub(unsigned int *a, unsigned int *b)
{
    unsigned int i;
    unsigned int x;
    unsigned int y;
    unsigned int borrow;
    unsigned int borrow1;
    unsigned int borrow2;

    i = N;
    borrow = 0;

    while (i != 0) {
        i = i - 1;

        if (a[i] < b[i])
            borrow1 = 1;
        else
            borrow1 = 0;

        x = a[i] - b[i];

        if (x < borrow)
            borrow2 = 1;
        else
            borrow2 = 0;

        y = x - borrow;

        a[i] = y;

        borrow = borrow1 | borrow2;
    }
}


/* --------------------------------------------------------- */
/* Small division                                            */
/* --------------------------------------------------------- */

/*
 * a /= d
 *
 * Binary restoring division.
 *
 * No:
 *
 *   *
 *   /
 *   %
 *
 * No variable shifts either.
 *
 * Because words are stored MSW first, division naturally
 * walks memory forward.
 */

void div_small(unsigned int *a, unsigned int d)
{
    unsigned int i;
    unsigned int bit;
    unsigned int x;
    unsigned int q;
    unsigned int rem;

    i = 0;
    rem = 0;

    while (i < N) {

        x = a[i];
        q = 0;

        bit = 32;

        while (bit != 0) {

            /*
             * Bring down the next input bit.
             */

            rem =
                (rem << 1)
                | (x >> 31);

            x = x << 1;


            /*
             * Generate the corresponding quotient bit.
             */

            q = q << 1;

            if (rem >= d) {
                rem = rem - d;
                q = q | 1;
            }

            bit = bit - 1;
        }

        a[i] = q;

        i = i + 1;
    }
}


/* --------------------------------------------------------- */
/* atan(1/x)                                                 */
/* --------------------------------------------------------- */

/*
 * atan(1/x) =
 *
 *   1/x
 * - 1/(3*x^3)
 * + 1/(5*x^5)
 * - ...
 *
 * power =
 *
 *   1/x^(2k+1)
 */

void arctan(
    unsigned int *result,
    unsigned int *power,
    unsigned int *temp,
    unsigned int x,
    unsigned int x2
)
{
    unsigned int odd;
    unsigned int sign;

    zero(result);

    set_one(power);
    div_small(power, x);

    odd = 1;
    sign = 0;

    while (nonzero(power) != 0) {

        copy(temp, power);

        if (odd != 1)
            div_small(temp, odd);

        if (sign == 0)
            add(result, temp);
        else
            sub(result, temp);

        /*
         * Move from:
         *
         *   1/x^(2k+1)
         *
         * to:
         *
         *   1/x^(2k+3)
         */

        div_small(power, x2);

        odd = odd + 2;
        sign = sign ^ 1;
    }
}


/* --------------------------------------------------------- */
/* Powers of two                                             */
/* --------------------------------------------------------- */

void shl1(unsigned int *a)
{
    unsigned int i;
    unsigned int x;
    unsigned int carry;
    unsigned int next_carry;

    i = N;
    carry = 0;

    while (i != 0) {
        i = i - 1;

        x = a[i];

        next_carry = x >> 31;

        a[i] =
            (x << 1)
            | carry;

        carry = next_carry;
    }
}


void shl2(unsigned int *a)
{
    shl1(a);
    shl1(a);
}


void shl4(unsigned int *a)
{
    shl1(a);
    shl1(a);
    shl1(a);
    shl1(a);
}


/* --------------------------------------------------------- */
/* Decimal output                                            */
/* --------------------------------------------------------- */

void print_digit(unsigned int x)
{
    if (x == 0)
        printf("0");
    else if (x == 1)
        printf("1");
    else if (x == 2)
        printf("2");
    else if (x == 3)
        printf("3");
    else if (x == 4)
        printf("4");
    else if (x == 5)
        printf("5");
    else if (x == 6)
        printf("6");
    else if (x == 7)
        printf("7");
    else if (x == 8)
        printf("8");
    else
        printf("9");
}


/*
 * a *= 10
 *
 * 10 = 8 + 2.
 *
 * No multiplication.
 */

void times10(unsigned int *a)
{
    unsigned int i;
    unsigned int x;
    unsigned int x8;
    unsigned int x2;
    unsigned int low;
    unsigned int carry;
    unsigned int next_carry;

    i = N;
    carry = 0;

    while (i != 0) {
        i = i - 1;

        x = a[i];

        x8 = x << 3;
        x2 = x << 1;

        /*
         * High 32 bits of x * 10:
         *
         * high(x*8) + high(x*2)
         * plus carry from adding the low halves.
         */

        next_carry =
            (x >> 29)
            + (x >> 31);

        low = x8 + x2;

        if (low < x8)
            next_carry = next_carry + 1;

        x = low + carry;

        if (x < low)
            next_carry = next_carry + 1;

        a[i] = x;
        carry = next_carry;
    }
}


/*
 * Print exactly:
 *
 *   "3." + 3838 fractional digits
 *
 * = 3840 characters.
 */

void print_pi(unsigned int *a)
{
    unsigned int i;
    unsigned int digit;

    print_digit(a[0]);
    printf(".");

    a[0] = 0;

    i = 0;

    while (i < 3838) {

        times10(a);

        digit = a[0];

        print_digit(digit);

        a[0] = 0;

        i = i + 1;
    }
}


/* --------------------------------------------------------- */
/* Main                                                      */
/* --------------------------------------------------------- */

int main()
{
    unsigned int a[402];
    unsigned int b[402];

    unsigned int power[402];
    unsigned int temp[402];


    /*
     * a = atan(1/5)
     */

    arctan(
        a,
        power,
        temp,
        5,
        25
    );


    /*
     * a *= 16
     */

    shl4(a);


    /*
     * b = atan(1/239)
     */

    arctan(
        b,
        power,
        temp,
        239,
        57121
    );


    /*
     * b *= 4
     */

    shl2(b);


    /*
     * Machin:
     *
     * pi =
     *
     *   16 atan(1/5)
     * -  4 atan(1/239)
     */

    sub(a, b);


    /*
     * 96 * 40 characters.
     */

    print_pi(a);

    return 0;
}
