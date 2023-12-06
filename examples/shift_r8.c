/*
   The purpose of this C program is to illustrate the functions shift_r8le()
   and shift_r8be(), which are called from shift_r8().
   These functions are symmetrical with the following replacements:

       PY_LITTLE_ENDIAN   <->   PY_BIG_ENDIAN
             <<=                   >>=
             >>                    <<

   Creating a macro from which both functions can be created is not possible,
   unless one replaces the existing preprocessor introductions with ordinary
   if statements.  For the sake of simplicity we do not want to do this here,
   even though it would avoid the spammish repetition.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define Py_ssize_t  ssize_t

#if (defined(__clang__) || defined(__GNUC__))
#define IS_GNUC  1
#else
#define IS_GNUC  0
#endif

/* machine byte-order */
#define PY_LITTLE_ENDIAN  1
#define PY_BIG_ENDIAN     0

/* bit-endianness */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

#define BITMASK(endian, i)  (((char) 1) << (endian == ENDIAN_LITTLE ? \
                                            ((i) % 8) : (7 - (i) % 8)))

/* The following two functions operate on first n bytes in buffer.
   Within this region, they shift all bits by k positions to right,
   i.e. towards higher addresses.
   They operate on little-endian and bit-endian bitarrays respectively.
   As we shift right, we need to start with the highest address and loop
   downwards such that lower bytes are still unaltered.
   See also examples/shift_r8.c
*/
static void
shift_r8le(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if IS_GNUC || PY_LITTLE_ENDIAN   /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] <<= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] >> (8 - k);
    }
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if IS_GNUC && PY_BIG_ENDIAN
        *p = __builtin_bswap64(*p);
        *p <<= k;
        *p = __builtin_bswap64(*p);
#else
        *p <<= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - k);
    }
}

static void
shift_r8be(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if IS_GNUC || PY_BIG_ENDIAN      /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] >>= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] << (8 - k);
    }
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if IS_GNUC && PY_LITTLE_ENDIAN
        *p = __builtin_bswap64(*p);
        *p >>= k;
        *p = __builtin_bswap64(*p);
#else
        *p >>= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] << (8 - k);
    }
}

/* display first nbits bytes of buffer given assumed bit-endianness
   to one line in stdout */
void display(unsigned char *buffer, Py_ssize_t nbits, int endian)
{
    Py_ssize_t i;

    for (i = 0; i < nbits; i++)
        printf("%d", (buffer[i / 8] & BITMASK(endian, i)) ? 1 : 0);

    printf("\n");
}

int main()
{
#define NBYTES  10
    unsigned char array[NBYTES] = {1, 15, 0, 131, 0, 255, 0, 7, 0, 1};
    ssize_t i;

    if ((PY_LITTLE_ENDIAN != (*((uint64_t *) "\xff\0\0\0\0\0\0\0") == 0xff))
        ||
        (PY_BIG_ENDIAN != (*((uint64_t *) "\0\0\0\0\0\0\0\xff") == 0xff))) {
        printf("Error: machine byte-order\n");
        return 1;
    }
    for (i = 0; i < 15; i++) {
        display(array, 77, ENDIAN_LITTLE);
        shift_r8le(array, NBYTES, 1);
    }
    for (i = 0; i < 15; i++) {
        display(array, 77, ENDIAN_BIG);
        shift_r8be(array, NBYTES, 1);
    }
    return 0;
}
