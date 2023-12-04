/*
   The purpose of this C program is to illustrate and document shift_r8()
   and in particular shift_r8le().

   In order take advantage of word shifts, it is necessary that the
   bit-order of the bitarray object is the same as the native machine
   byte-order.

   The function shift_r8() dispatches to shift_r8le() and shift_r8be().

   shift_r8le() assumes the buffer represents a bitarray with little-endian
   bit-endianness.  It takes advantage of word shifts on little-endian
   machines.

   shift_r8be() assumes the buffer represents a bitarray with big-endian
   bit-endianness.  It performs byte shifts only.
   It would be possible to also take advantage of word shift for big-endian
   bitarrays on big-endian machines in this function.  However, as big-endian
   machines are very rare, this is not being done.
   Another approach to would be to the following function body:

       bytereverse(buff, k);
       shift_r8le(buff, k, n);
       bytereverse(buff, k);

   While this takes advantage of word shifts of big-endian bitarrays on
   little-endian machines, it requires two bytereverse() calls.
   There is no speed improvement of this approach over performing byte shifts
   only.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define Py_ssize_t  ssize_t

/* machine byte-order */
#define PY_LITTLE_ENDIAN  (*((uint64_t *) "\xff\0\0\0\0\0\0\0") == 0xff)

/* bit endianness */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

#define BITMASK(endian, i)  (((char) 1) << (endian == ENDIAN_LITTLE ? \
                                            ((i) % 8) : (7 - (i) % 8)))

/* Shift k bytes in buffer by n bits to right (towards higher addresses),
   using uint64 (word) shifts when possible.

   As we shift bits right, we need to start with the highest address
   and loop downwards such that "carry" bytes are still unaltered.

   This function assumes that the buffer represents a bitarray with
   little-endian bit-endianness.
*/
void shift_r8le(unsigned char *buff, Py_ssize_t k, int n)
{
    Py_ssize_t w = 0;

    if (PY_LITTLE_ENDIAN) {       /* use shift word */
        w = k / 8;                /* number of words used for shifting */
        k %= 8;                   /* number of remaining bytes */
    }
    while (k--) {                 /* shift in byte-range(8 * w, k) */
        Py_ssize_t i = k + 8 * w;
        buff[i] <<= n;            /* shift byte */
        if (k || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] >> (8 - n);
    }
    while (w--) {                 /* shift in word-range(0, w) */
        ((uint64_t *) buff)[w] <<= n;  /* shift word */
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - n);
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

    printf("machine byte-order: %s\n", PY_LITTLE_ENDIAN ? "little" : "big");

    for (i = 0; i < 30; i++) {
        /* Try changing this to ENDIAN_BIG and see what happens! */
        display(array, 77, ENDIAN_LITTLE);
        shift_r8le(array, NBYTES, 1);
    }
    return 0;
}
