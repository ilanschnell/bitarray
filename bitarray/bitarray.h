/*
   Copyright (c) 2008 - 2021, Ilan Schnell
   bitarray is published under the PSF license.

   Author: Ilan Schnell
*/
#define BITARRAY_VERSION  "1.6.2"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

#if PY_MAJOR_VERSION == 2
/* the Py_MIN macro was introduced in Python 3.3 */
#define Py_MIN(x, y)  (((x) > (y)) ? (y) : (x))
#define PySlice_GetIndicesEx(slice, len, start, stop, step, slicelength) \
    PySlice_GetIndicesEx(((PySliceObject *) slice),                      \
                         (len), (start), (stop), (step), (slicelength))
#define PyLong_FromLong  PyInt_FromLong
#endif

/* ob_size is the byte count of the buffer, not the number of elements.
   The number of elements (bits) is nbits. */
typedef struct {
    PyObject_VAR_HEAD
    char *ob_item;              /* buffer */
    Py_ssize_t allocated;       /* how many bytes allocated */
    Py_ssize_t nbits;           /* length of bitarray, i.e. elements */
    int endian;                 /* bit endianness of bitarray */
    int ob_exports;             /* how many buffer exports */
    PyObject *weakreflist;      /* list of weak references */
} bitarrayobject;

/* --- bit endianness --- */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

/* returns the endianness string from bitarrayobject */
#define ENDIAN_STR(o)  ((o)->endian == ENDIAN_LITTLE ? "little" : "big")

/* number of bits that can be stored in given bytes */
#define BITS(bytes)  ((bytes) << 3)

/* number of bytes necessary to store given bits */
#define BYTES(bits)  ((bits) == 0 ? 0 : (((bits) - 1) / 8 + 1))

#define BITMASK(endian, i)  \
    (((char) 1) << ((endian) == ENDIAN_LITTLE ? ((i) % 8) : (7 - (i) % 8)))

/* ------------ low level access to bits in bitarrayobject ------------- */

#ifndef NDEBUG
static inline int GETBIT(bitarrayobject *self, Py_ssize_t i) {
    assert(0 <= i && i < self->nbits);
    return ((self)->ob_item[(i) / 8] & BITMASK((self)->endian, i) ? 1 : 0);
}
#else
#define GETBIT(self, i)  \
    ((self)->ob_item[(i) / 8] & BITMASK((self)->endian, i) ? 1 : 0)
#endif

static inline void
setbit(bitarrayobject *self, Py_ssize_t i, int bit)
{
    char *cp, mask;

    assert(0 <= i && i < BITS(Py_SIZE(self)));
    mask = BITMASK(self->endian, i);
    cp = self->ob_item + i / 8;
    if (bit)
        *cp |= mask;
    else
        *cp &= ~mask;
}

/* sets unused padding bits (within last byte of buffer) to 0,
   and return the number of padding bits -- self->nbits is unchanged */
static inline int
setunused(bitarrayobject *self)
{
    const char mask[16] = {
        /* elements 0 and 8 (with value 0x00) are never accessed */
        0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, /* little endian */
        0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, /* big endian */
    };
    int res;

    if (self->nbits % 8 == 0)
        return 0;

    res = (int) (BITS(Py_SIZE(self)) - self->nbits);
    assert(0 < res && res < 8);
    /* apply the appropriate mask to the last byte in buffer */
    self->ob_item[Py_SIZE(self) - 1] &=
        mask[self->nbits % 8 + (self->endian == ENDIAN_LITTLE ? 0 : 8)];

    return res;
}

static unsigned char bitcount_lookup[256] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
};


#if (defined(__GNUC__) || defined(__clang__))
#define HAS_VECTORS 1
#else
#define HAS_VECTORS 0
#endif

#if HAS_VECTORS
typedef char vec __attribute__((vector_size(16)));
#endif

#if (defined(__GNUC__) || defined(__clang__))
#define ATTR_UNUSED __attribute__((__unused__))
#else
#define ATTR_UNUSED
#endif

#define UNUSEDVAR(x) (void)x

#if HAS_VECTORS
/*
  * Perform bitwise operation OP on 16 bytes of memory at a time.
  */
 #define vector_op(A, B, OP) do {  \
     vec __a, __b, __r;            \
     memcpy(&__a, A, sizeof(vec)); \
     memcpy(&__b, B, sizeof(vec)); \
     __r = __a OP __b;             \
     memcpy(A, &__r, sizeof(vec)); \
 } while(0);
#endif