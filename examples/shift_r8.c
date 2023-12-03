/*
   This C program implements the (slightly modified) function shift_r8() in
   order to better illustrates how it works.  There are also more comments
   and notes than in _bitarray.c .
*/
#include <stdio.h>
#include <stdlib.h>

/* machine byte-order */
#define PY_LITTLE_ENDIAN  (*((uint64_t *) "\xff\0\0\0\0\0\0\0") == 0xff)

/* bit endianness */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

#define BITMASK(endian, i)  (((char) 1) << (endian == ENDIAN_LITTLE ? \
                                            ((i) % 8) : (7 - (i) % 8)))

/* Shift bits in byte-range(0, k) by n bits to right (towards
   higher addresses), using uint64 (word) shifts when possible.

   Notes:

     - As we shift bits right, we need to start with the highest address
       and loop downwards such that carry bytes are still unchanged.

     - In order take advantage of word shifts, it is necessary that the
       bit-order of the bitarray object is the same as the native
       machine byte-order.  Hence, the core part of this function is
       designed for little-endian bitarrays such that word shifts can
       be used on (more common) little-endian machines.

     - Also, in the production function bitarrays with big-endian (bit-order)
       representation are byte reversed at the beginning, and (re-) reverse
       again at the end.

     - In the production function, we apply the offset 'a' to the buffer.
       For simplicity, we don't this here.
*/
void shift_r8(unsigned char *ucbuff, int k, int n)
{
    int i, m = 8 - n, w = 0, v = 0;

    if (PY_LITTLE_ENDIAN) {       /* use shift word */
        w = k / 8;                /* number of words used for shifting */
        v = 8 * w;                /* number of bytes in those words */
    }

    /* shift in byte-range(v, k) -> with offset byte-range(a + v, b) */
    for (i = k - 1; i >= v; i--) {
        ucbuff[i] <<= n;          /* shift byte (from highest to lowest) */
        if (w || i != v)          /* add shifted next lower byte */
            ucbuff[i] |= ucbuff[i - 1] >> m;
    }

    /* shift in word-range(0, w) -> with offset byte-range(a, a + v) */
    while (w--) {
        ((uint64_t *) ucbuff)[w] <<= n;
        if (w)                    /* add shifted byte from next lower word */
            ucbuff[8 * w] |= ucbuff[8 * w - 1] >> m;
    }
}

/* display first nbytes bytes of buffer given assumed bit-endianness
   to one line in stdout */
void display(unsigned char *buffer, int nbytes, int endian)
{
    int i;

    for (i = 0; i < 8 * nbytes; i++)
        printf("%d", (buffer[i / 8] & BITMASK(endian, i)) ? 1 : 0);

    printf("\n");
}

int main()
{
    const int nbytes = 10;
    unsigned char array[nbytes] =
        {1, 15, 98, 149, 231, 36, 90, 255, 44, 145};
    int i;

    printf("PY_LITTLE_ENDIAN: %d\n", PY_LITTLE_ENDIAN);

    for (i = 0; i < 30; i++) {
        display(array, nbytes, ENDIAN_LITTLE);
        shift_r8(array, nbytes, 1);
    }
    return 0;
}
