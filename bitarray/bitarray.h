/*
   Copyright (c) 2008 - 2022, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   Author: Ilan Schnell
*/
#define BITARRAY_VERSION  "2.6.0"

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

/* Py_UNREACHABLE was introduced in Python 3.7 */
#ifndef Py_UNREACHABLE
#define Py_UNREACHABLE() abort()
#endif

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#define BYTES_SIZE_FMT  "y#"
#else
/* the Py_MIN and Py_MAX macros were introduced in Python 3.3 */
#define Py_MIN(x, y)  (((x) > (y)) ? (y) : (x))
#define Py_MAX(x, y)  (((x) > (y)) ? (x) : (y))
#define PySlice_GetIndicesEx(slice, len, start, stop, step, slicelength) \
    PySlice_GetIndicesEx(((PySliceObject *) slice),                      \
                         (len), (start), (stop), (step), (slicelength))
#define PyLong_FromLong  PyInt_FromLong
#define BYTES_SIZE_FMT  "s#"
#endif

/* --- bitarrayobject --- */

/* .ob_size is buffer size (in bytes), not the number of elements.
   The number of elements (bits) is .nbits. */
typedef struct {
    PyObject_VAR_HEAD
    char *ob_item;              /* buffer */
    Py_ssize_t allocated;       /* allocated buffer size (in bytes) */
    Py_ssize_t nbits;           /* length of bitarray, i.e. elements */
    int endian;                 /* bit endianness of bitarray */
    int ob_exports;             /* how many buffer exports */
    PyObject *weakreflist;      /* list of weak references */
    Py_buffer *buffer;          /* used when importing a buffer */
    int readonly;               /* buffer is readonly */
} bitarrayobject;

/* --- bit endianness --- */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1

#define IS_LE(self)  ((self)->endian == ENDIAN_LITTLE)
#define IS_BE(self)  ((self)->endian == ENDIAN_BIG)

/* the endianness string */
#define ENDIAN_STR(endian)  ((endian) == ENDIAN_LITTLE ? "little" : "big")

/* number of pad bits */
#define PADBITS(self)  (8 * Py_SIZE(self) - (self)->nbits)

/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) + 7) >> 3)

/* we're not using bitmask_table here, as it is actually slower */
#define BITMASK(self, i)  (((char) 1) << ((self)->endian == ENDIAN_LITTLE ? \
                                          ((i) % 8) : (7 - (i) % 8)))

/* assert that .nbits is in agreement with .ob_size */
#define assert_nbits(self)  assert(BYTES((self)->nbits) == Py_SIZE(self))

/* assert byte index is in range */
#define assert_byte_in_range(self, j)  \
    assert(self->ob_item && 0 <= (j) && (j) < Py_SIZE(self))

/* ------------ low level access to bits in bitarrayobject ------------- */

static inline int
getbit(bitarrayobject *self, Py_ssize_t i)
{
    assert_nbits(self);
    assert(0 <= i && i < self->nbits);
    return self->ob_item[i >> 3] & BITMASK(self, i) ? 1 : 0;
}

static inline void
setbit(bitarrayobject *self, Py_ssize_t i, int vi)
{
    char *cp, mask;

    assert_nbits(self);
    assert(0 <= i && i < self->nbits);
    assert(self->readonly == 0);

    mask = BITMASK(self, i);
    cp = self->ob_item + (i >> 3);
    if (vi)
        *cp |= mask;
    else
        *cp &= ~mask;
}

static const char bitmask_table[2][8] = {
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},  /* little endian */
    {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01},  /* big endian */
};

/* character with n leading ones is: ones_table[endian][n] */
static const char ones_table[2][8] = {
    {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f},  /* little endian */
    {0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe},  /* big endian */
};

/* Return last byte in buffer with pad bits zeroed out.  The the number of
   bits in the bitarray must not be a multiple of 8. */
static inline char
zeroed_last_byte(bitarrayobject *self)
{
    const int r = self->nbits % 8;     /* index into mask table */

    assert(r > 0);
    assert_nbits(self);
    return ones_table[IS_BE(self)][r] & self->ob_item[Py_SIZE(self) - 1];
}

/* Unless buffer is readonly, zero out pad bits.
   Always return the number of pad bits - leave self->nbits unchanged */
static inline int
set_padbits(bitarrayobject *self)
{
    const int r = self->nbits % 8;

    if (r == 0)
        return 0;
    if (self->readonly == 0)
        self->ob_item[Py_SIZE(self) - 1] = zeroed_last_byte(self);
    return 8 - r;
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

/* adjust index a manner consistent with the handling of normal slices */
static inline void
adjust_index(Py_ssize_t length, Py_ssize_t *i, Py_ssize_t step)
{
    if (*i < 0) {
        *i += length;
        if (*i < 0)
            *i = (step < 0) ? -1 : 0;
    }
    else if (*i >= length) {
        *i = (step < 0) ? length - 1 : length;
    }
}

/* same as PySlice_AdjustIndices() which was introduced in Python 3.6.1 */
static inline Py_ssize_t
adjust_indices(Py_ssize_t length, Py_ssize_t *start, Py_ssize_t *stop,
               Py_ssize_t step)
{
#if PY_VERSION_HEX > 0x03060100
    return PySlice_AdjustIndices(length, start, stop, step);
#else
    assert(step != 0);
    adjust_index(length, start, step);
    adjust_index(length, stop, step);
    /*
      a / b does integer division.  If either a or b is negative, the result
      depends on the compiler (rounding can go toward 0 or negative infinity).
      Therefore, we are careful that both a and b are always positive.
    */
    if (step < 0) {
        if (*stop < *start)
            return (*start - *stop - 1) / (-step) + 1;
    }
    else {
        if (*start < *stop)
            return (*stop - *start - 1) / step + 1;
    }
    return 0;
#endif
}

/* adjust slice parameters such that step is always positive; produces
   simpler loops over elements when their order is irrelevant */
static inline void
adjust_step_positive(Py_ssize_t slicelength,
                     Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step)
{
    if (*step < 0) {
        *stop = *start + 1;
        *start = *stop + *step * (slicelength - 1) - 1;
        *step = -(*step);
    }
    assert(*start >= 0 && *stop >= 0 && *step > 0 && slicelength >= 0);
    /* slicelength == 0 implies stop <= start */
    assert(slicelength != 0 || *stop <= *start);
    /* step == 1 and slicelength != 0 implies stop - start == slicelength */
    assert(*step != 1 || slicelength == 0 || *stop - *start == slicelength);
}

/* convert Python object to C int and set value at address -
   return 1 on success, 0 on failure (and raise exception) */
static inline int
conv_pybit(PyObject *value, int *vi)
{
    Py_ssize_t n;

    n = PyNumber_AsSsize_t(value, NULL);
    if (n == -1 && PyErr_Occurred())
        return 0;

    if (n < 0 || n > 1) {
        PyErr_Format(PyExc_ValueError, "bit must be 0 or 1, got %zd", n);
        return 0;
    }
    *vi = (int) n;
    return 1;
}
