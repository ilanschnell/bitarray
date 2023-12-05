/*
   The purpose of this C program is to illustrate and document shift_r8(),
   in particular the functions shift_r8le() and shift_r8be().

   The function shift_r8() dispatches to shift_r8le() and shift_r8be().
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

/* bit endianness */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

#define BITMASK(endian, i)  (((char) 1) << (endian == ENDIAN_LITTLE ? \
                                            ((i) % 8) : (7 - (i) % 8)))

/* As we shift bits right, we need to start with the highest address
   and loop downwards such that "carry" bytes are still unaltered.
*/
static void
shift_r8le(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if PY_LITTLE_ENDIAN              /* use shift word */
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
        ((uint64_t *) buff)[w] <<= k;  /* shift word */
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - k);
    }
}

static void
shift_r8be(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if PY_LITTLE_ENDIAN && IS_GNUC   /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift bytes (from highest to lowest) */
        Py_ssize_t i = n + 8 * w;
        buff[i] >>= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] << (8 - k);
    }
#if IS_GNUC
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
        *p = __builtin_bswap64(*p);    /* swap bytes in word  */
        *p >>= k;                      /* shift word */
        *p = __builtin_bswap64(*p);    /* swap bytes in word */
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] << (8 - k);
    }
#endif
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

    if (PY_LITTLE_ENDIAN != (*((uint64_t *) "\xff\0\0\0\0\0\0\0") == 0xff)) {
        printf("Error: wrong PY_LITTLE_ENDIAN\n");
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
