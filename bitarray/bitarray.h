/*
   Copyright (c) 2008 - 2021, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   Author: Ilan Schnell
*/
#define BITARRAY_VERSION  "1.9.2"

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

/* the endianness string from bitarrayobject */
#define ENDIAN_STR(o)  ((o)->endian == ENDIAN_LITTLE ? "little" : "big")

/* number of bits that can be stored in given bytes */
#define BITS(bytes)  ((bytes) << 3)

/* number of bytes necessary to store given bits */
#define BYTES(bits)  ((bits) == 0 ? 0 : (((bits) - 1) / 8 + 1))

#define BITMASK(endian, i)  \
    (((char) 1) << ((endian) == ENDIAN_LITTLE ? ((i) % 8) : (7 - (i) % 8)))

/* ------------ low level access to bits in bitarrayobject ------------- */

#ifndef NDEBUG
static inline int GETBIT(bitarrayobject *self, Py_ssize_t i)
{
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

static const unsigned char bitcount_lookup[256] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
};

/* Given a PyLong (which must be 0 or 1) or a PyBool, return 0 or 1.
   On error, return -1 and set error message. */
static inline int
IntOrBool_AsInt(PyObject *v)
{
    Py_ssize_t x;

    x = PyNumber_AsSsize_t(v, NULL);
    if (x == -1 && PyErr_Occurred())
        return -1;

    if (x < 0 || x > 1) {
        PyErr_SetString(PyExc_ValueError, "bit must be 0 or 1");
        return -1;
    }
    return (int) x;
}
