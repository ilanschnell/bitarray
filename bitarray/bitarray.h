/*
   Copyright (c) 2008 - 2021, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   Author: Ilan Schnell
*/
#define BITARRAY_VERSION  "2.2.1"

/* .ob_size is the byte count of the buffer, not the number of elements.
   The number of elements (bits) is .nbits. */
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

/* assert that .nbits is in agreement with .ob_size */
#define assert_nbits(self)  assert(BYTES((self)->nbits) == Py_SIZE(self))

/* --------------- definitions not specific to bitarray ---------------- */

#ifdef STDC_HEADERS
#include <stddef.h>
#else  /* !STDC_HEADERS */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      /* For size_t */
#endif /* HAVE_SYS_TYPES_H */
#endif /* !STDC_HEADERS */

/* Compatibility with Visual Studio 2013 and older which don't support
   the inline keyword in C (only in C++): use __inline instead.
   (copied from pythoncapi_compat.h) */
#if (defined(_MSC_VER) && _MSC_VER < 1900 \
     && !defined(__cplusplus) && !defined(inline))
#define inline __inline
#endif

/* --- definitions specific to Python --- */

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#define BYTES_SIZE_FMT  "y#"
#else
/* the Py_MIN macro was introduced in Python 3.3 */
#define Py_MIN(x, y)  (((x) > (y)) ? (y) : (x))
#define PySlice_GetIndicesEx(slice, len, start, stop, step, slicelength) \
    PySlice_GetIndicesEx(((PySliceObject *) slice),                      \
                         (len), (start), (stop), (step), (slicelength))
#define PyLong_FromLong  PyInt_FromLong
#define BYTES_SIZE_FMT  "s#"
#endif

/* ------------ low level access to bits in bitarrayobject ------------- */

static inline int
getbit(bitarrayobject *self, Py_ssize_t i)
{
    assert(0 <= i && i < self->nbits && i / 8 < Py_SIZE(self));
    assert_nbits(self);
    return (self->ob_item[i / 8] & BITMASK(self->endian, i) ? 1 : 0);
}

static inline void
setbit(bitarrayobject *self, Py_ssize_t i, int bit)
{
    char *cp, mask;

    assert(0 <= i && i < self->nbits && i / 8 < Py_SIZE(self));
    assert_nbits(self);
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
    const Py_ssize_t nbits = self->nbits;
    int res;

    if (nbits % 8 == 0)
        return 0;

    res = (int) (BITS(BYTES(nbits)) - nbits);
    assert(0 < res && res < 8 && Py_SIZE(self) > 0);
    assert_nbits(self);
    /* apply the appropriate mask to the last byte in buffer */
    self->ob_item[Py_SIZE(self) - 1] &=
        mask[nbits % 8 + (self->endian == ENDIAN_LITTLE ? 0 : 8)];

    return res;
}

static const unsigned char bitcount_lookup[256] = {
#define B2(n)  n, n + 1, n + 1, n + 2
#define B4(n)  B2(n), B2(n + 1), B2(n + 1), B2(n + 2)
#define B6(n)  B4(n), B4(n + 1), B4(n + 1), B4(n + 2)
    B6(0), B6(1), B6(1), B6(2)
#undef B2
#undef B4
#undef B6
};

/* Interpret a PyObject (usually PyLong or PyBool) as a bit, return 0 or 1.
   On error, return -1 and set error message. */
static inline int
pybit_as_int(PyObject *v)
{
    Py_ssize_t x;

    x = PyNumber_AsSsize_t(v, NULL);
    if (x == -1 && PyErr_Occurred())
        return -1;

    if (x < 0 || x > 1) {
        PyErr_Format(PyExc_ValueError, "bit must be 0 or 1, got %zd", x);
        return -1;
    }
    return (int) x;
}
