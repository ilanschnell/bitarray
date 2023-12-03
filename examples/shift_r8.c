/*
   This C program implements the function shift_r8le() in order to better
   illustrate and document it.
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

/* Shift k bytes in buffer by n bits to right (towards higher addresses),
   using uint64 (word) shifts when possible.

   Notes:

     - As we shift bits right, we need to start with the highest address
       and loop downwards such that carry bytes are still unchanged.

     - In order take advantage of word shifts, it is necessary that the
       bit-order of the bitarray object is the same as the native
       machine byte-order.  Hence, the core part of this function is
       designed for little-endian bitarrays such that word shifts can
       be used on (more common) little-endian machines.

     - In the production function, bitarrays with big-endian (bit-order)
       representation are byte reversed at the beginning, and (re-) reverse
       again at the end.  This is not done here.  This function assumes
       bitarrays with little-endian bit-endianness.

     - Also, in the production function, we apply the offset 'a' to the
       buffer.  For simplicity, we don't this here.
*/
void shift_r8le(unsigned char *buff, int k, int n)
{
    int w = 0;

    if (PY_LITTLE_ENDIAN) {       /* use shift word */
        w = k / 8;                /* number of words used for shifting */
        k %= 8;                   /* number of additional bytes */
    }

    /* shift in byte-range(8 * w, k) */
    while (k--) {
        int i = k + 8 * w;
        buff[i] <<= n;            /* shift byte (from highest to lowest) */
        if (k || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] >> (8 - n);
    }

    /* shift in word-range(0, w) */
    while (w--) {
        ((uint64_t *) buff)[w] <<= n;  /* shift word */
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - n);
    }
}

/* display first nbits bytes of buffer given assumed bit-endianness
   to one line in stdout */
void display(unsigned char *buffer, int nbits, int endian)
{
    int i;

    for (i = 0; i < nbits; i++)
        printf("%d", (buffer[i / 8] & BITMASK(endian, i)) ? 1 : 0);

    printf("\n");
}

int main()
{
    const int nbytes = 10;
    unsigned char array[nbytes] = {1, 15, 0, 131, 0, 255, 0, 7, 0, 1};
    int i;

    printf("machine byte-order: %d\n", PY_LITTLE_ENDIAN);

    for (i = 0; i < 30; i++) {
        /* Try changing this to ENDIAN_BIG and see what happens! */
        display(array, 77, ENDIAN_LITTLE);
        shift_r8le(array, nbytes, 1);
    }
    return 0;
}
