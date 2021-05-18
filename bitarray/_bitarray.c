/*
   Copyright (c) 2008 - 2021, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   This file is the C part of the bitarray package.
   All functionality of the bitarray object is implemented here.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "bitarray.h"

/* size used when reading / writing blocks from files (in bytes) */
#define BLOCKSIZE  65536

#ifdef IS_PY3K
#define Py_TPFLAGS_HAVE_WEAKREFS  0
#define BYTES_SIZE_FMT  "y#"
#else
#define BYTES_SIZE_FMT  "s#"
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#else  /* !STDC_HEADERS */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      /* For size_t */
#endif /* HAVE_SYS_TYPES_H */
#endif /* !STDC_HEADERS */

static int default_endian = ENDIAN_BIG;

static PyTypeObject Bitarray_Type;

#define bitarray_Check(obj)  PyObject_TypeCheck((obj), &Bitarray_Type)


static int
resize(bitarrayobject *self, Py_ssize_t nbits)
{
    const Py_ssize_t allocated = self->allocated, size = Py_SIZE(self);
    Py_ssize_t newsize;
    size_t new_allocated;

    assert(allocated >= size && size == BYTES(self->nbits));
    /* ob_item == NULL implies ob_size == allocated == 0 */
    assert(self->ob_item != NULL || (size == 0 && allocated == 0));
    /* allocated == 0 implies size == 0 */
    assert(allocated != 0 || size == 0);

    newsize = BYTES(nbits);
    if (nbits < 0 || BITS(newsize) < 0) {
        PyErr_Format(PyExc_OverflowError, "bitarray resize %zd", nbits);
        return -1;
    }

    if (newsize == size) {
        /* buffer size hasn't changed - bypass everything */
        self->nbits = nbits;
        return 0;
    }

    if (self->ob_exports > 0) {
        PyErr_SetString(PyExc_BufferError,
                        "cannot resize bitarray that is exporting buffers");
        return -1;
    }

    /* Bypass reallocation when a allocation is large enough to accommodate
       the newsize.  If the newsize falls lower than half the allocated size,
       then proceed with the reallocation to shrink the bitarray.
    */
    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        assert(self->ob_item != NULL || newsize == 0);
        Py_SET_SIZE(self, newsize);
        self->nbits = nbits;
        return 0;
    }

    if (newsize == 0) {
        PyMem_FREE(self->ob_item);
        self->ob_item = NULL;
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        return 0;
    }

    new_allocated = (size_t) newsize;
    if (size == 0 && newsize <= 4)
        /* When resizing an empty bitarray, we want at least 4 bytes. */
        new_allocated = 4;

    /* Over-allocate when the (previous) size is non-zero (as we often
       extend an empty array on creation) and the size is actually
       increasing. */
    else if (size != 0 && newsize > size)
        /* This over-allocates proportional to the bitarray size, making
           room for additional growth.
           The growth pattern is:  0, 4, 8, 16, 25, 34, 44, 54, 65, 77, ...
           The pattern starts out the same as for lists but then
           grows at a smaller rate so that larger bitarrays only overallocate
           by about 1/16th -- this is done because bitarrays are assumed
           to be memory critical. */
        new_allocated += (newsize >> 4) + (newsize < 8 ? 3 : 7);

    assert(new_allocated >= (size_t) newsize);
    self->ob_item = PyMem_Realloc(self->ob_item, new_allocated);
    if (self->ob_item == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    Py_SET_SIZE(self, newsize);
    self->allocated = new_allocated;
    self->nbits = nbits;
    return 0;
}

/* create new bitarray object without initialization of buffer */
static PyObject *
newbitarrayobject(PyTypeObject *type, Py_ssize_t nbits, int endian)
{
    const Py_ssize_t nbytes = BYTES(nbits);
    bitarrayobject *obj;

    assert(nbits >= 0);
    obj = (bitarrayobject *) type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    Py_SET_SIZE(obj, nbytes);
    if (nbytes == 0) {
        obj->ob_item = NULL;
    }
    else {
        obj->ob_item = (char *) PyMem_Malloc((size_t) nbytes);
        if (obj->ob_item == NULL) {
            PyObject_Del(obj);
            return PyErr_NoMemory();
        }
    }
    obj->allocated = nbytes;
    obj->nbits = nbits;
    obj->endian = endian;
    obj->ob_exports = 0;
    obj->weakreflist = NULL;
    return (PyObject *) obj;
}

static void
bitarray_dealloc(bitarrayobject *self)
{
    if (self->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);

    if (self->ob_item != NULL)
        PyMem_Free((void *) self->ob_item);

    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* reverse n bytes (starting at start) in buffer */
static void
bytereverse(bitarrayobject *self, Py_ssize_t start, Py_ssize_t n)
{
    static char trans[256];
    static int setup = 0;
    Py_ssize_t i, stop = start + n;

    assert(n >= 0);
    assert(0 <= start && start <= Py_SIZE(self));
    assert(0 <= stop && stop <= Py_SIZE(self));

    if (!setup) {
        /* setup translation table, which maps each byte to it's reversed:
           trans = {0, 128, 64, 192, 32, 160, ..., 255} */
        int j, k;

        for (k = 0; k < 256; k++) {
            trans[k] = 0x00;
            for (j = 0; j < 8; j++)
                if (1 << (7 - j) & k)
                    trans[k] |= 1 << j;
        }
        setup = 1;
    }

    setunused(self);
    for (i = start; i < stop; i++)
        self->ob_item[i] = trans[(unsigned char) self->ob_item[i]];
}

/* copy n bits from other (starting at b) onto self (starting at a) */
static void
copy_n(bitarrayobject *self, Py_ssize_t a,
       bitarrayobject *other, Py_ssize_t b, Py_ssize_t n)
{
    Py_ssize_t i;

    assert(0 <= n && n <= self->nbits && n <= other->nbits);
    assert(0 <= a && a <= self->nbits - n);
    assert(0 <= b && b <= other->nbits - n);
    if (n == 0)
        return;

    /* When the start positions are at byte positions, we can copy whole
       bytes using memmove, and copy the remaining few bits individually.
       Note that the order of these two operations matters when copying
       self to self. */
    if (a % 8 == 0 && b % 8 == 0 && n >= 8) {
        const size_t bytes = n / 8;
        const Py_ssize_t bits = BITS(bytes);

        assert(bytes > 0 && bits <= n && n < bits + 8);
        if (a > b)
            copy_n(self, bits + a, other, bits + b, n - bits);

        memmove(self->ob_item + a / 8, other->ob_item + b / 8, bytes);
        if (self->endian != other->endian)
            bytereverse(self, a / 8, bytes);

        if (a <= b)
            copy_n(self, bits + a, other, bits + b, n - bits);

        return;
    }

    /* The two different types of looping are only relevant when copying
       self to self, i.e. when copying a piece of an bitarrayobject onto
       itself. */
    if (a <= b) {
        for (i = 0; i < n; i++)             /* loop forward (delete) */
            setbit(self, i + a, GETBIT(other, i + b));
    }
    else {
        for (i = n - 1; i >= 0; i--)      /* loop backwards (insert) */
            setbit(self, i + a, GETBIT(other, i + b));
    }
}

/* starting at start, delete n bits from self */
static int
delete_n(bitarrayobject *self, Py_ssize_t start, Py_ssize_t n)
{
    assert(0 <= start && start <= self->nbits);
    assert(0 <= n && n <= self->nbits - start);

    copy_n(self, start, self, start + n, self->nbits - start - n);
    return resize(self, self->nbits - n);
}

/* starting at start, insert n (uninitialized) bits into self */
static int
insert_n(bitarrayobject *self, Py_ssize_t start, Py_ssize_t n)
{
    assert(0 <= start && start <= self->nbits);
    assert(n >= 0);

    if (resize(self, self->nbits + n) < 0)
        return -1;
    copy_n(self, start + n, self, start, self->nbits - start - n);
    return 0;
}

static void
invert(bitarrayobject *self)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    Py_ssize_t i;

    for (i = 0; i < nbytes; i++)
        self->ob_item[i] = ~self->ob_item[i];
}

/* repeat self n times (negative n is treated as 0) */
static int
repeat(bitarrayobject *self, Py_ssize_t n)
{
    const Py_ssize_t nbits = self->nbits;
    Py_ssize_t i;

    if (nbits == 0 || n == 1)   /* nothing to do */
        return 0;

    if (n <= 0)                 /* clear */
        return resize(self, 0);

    assert(n > 1 && nbits > 0);
    if (nbits > PY_SSIZE_T_MAX / n) {
        PyErr_Format(PyExc_OverflowError,
                     "cannot repeat bitarray (of size %zd) %zd times",
                     nbits, n);
        return -1;
    }

    if (resize(self, n * nbits) < 0)
        return -1;

    for (i = 1; i < n; i++)
        copy_n(self, i * nbits, self, 0, nbits);

    return 0;
}

/* set the bits from start to stop (excluding) in self to val */
static void
setrange(bitarrayobject *self, Py_ssize_t start, Py_ssize_t stop, int val)
{
    Py_ssize_t i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (self->nbits == 0 || start >= stop)
        return;

    if (stop >= start + 8) {
        const Py_ssize_t byte_start = BYTES(start);
        const Py_ssize_t byte_stop = stop / 8;

        for (i = start; i < BITS(byte_start); i++)
            setbit(self, i, val);
        memset(self->ob_item + byte_start, val ? 0xff : 0x00,
               (size_t) (byte_stop - byte_start));
        for (i = BITS(byte_stop); i < stop; i++)
            setbit(self, i, val);
    }
    else {
        for (i = start; i < stop; i++)
            setbit(self, i, val);
    }
}

/* Return number of 'vi' bits in range(start, stop).
   This function never fails. */
static Py_ssize_t
count(bitarrayobject *self, int vi, Py_ssize_t start, Py_ssize_t stop)
{
    Py_ssize_t res = 0, i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);
    assert(0 <= vi && vi <= 1);
    assert(BYTES(stop) <= Py_SIZE(self));

    if (self->nbits == 0 || start >= stop)
        return 0;

    if (stop >= start + 8) {
        const Py_ssize_t byte_start = BYTES(start);
        const Py_ssize_t byte_stop = stop / 8;
        Py_ssize_t j;

        for (i = start; i < BITS(byte_start); i++)
            res += GETBIT(self, i);
        for (j = byte_start; j < byte_stop; j++)
            res += bitcount_lookup[(unsigned char) self->ob_item[j]];
        for (i = BITS(byte_stop); i < stop; i++)
            res += GETBIT(self, i);
    }
    else {
        for (i = start; i < stop; i++)
            res += GETBIT(self, i);
    }
    return vi ? res : stop - start - res;
}

/* Return index of first occurrence of vi, and -1 when vi is not found.
   This function never fails. */
static Py_ssize_t
find_bit(bitarrayobject *self, int vi, Py_ssize_t start, Py_ssize_t stop)
{
    Py_ssize_t i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);
    assert(0 <= vi && vi <= 1);
    assert(BYTES(stop) <= Py_SIZE(self));

    if (self->nbits == 0 || start >= stop)
        return -1;

    if (stop >= start + 8) {
        /* seraching for 1 means: break when byte is not 0x00
           searching for 0 means: break when byte is not 0xff */
        const char c = vi ? 0x00 : 0xff;

        /* skip ahead by checking whole bytes */
        for (i = start / 8; i < BYTES(stop); i++) {
            if (c ^ self->ob_item[i])
                break;
        }
        if (start < BITS(i))
            start = BITS(i);
    }

    /* fine grained search */
    for (i = start; i < stop; i++) {
        if (GETBIT(self, i) == vi)
            return i;
    }
    return -1;
}

/* Return first occurrence of bitarray xa (in self), such that xa is contained
   within self[start:stop], or -1 when xa is not found */
static Py_ssize_t
find(bitarrayobject *self, bitarrayobject *xa,
     Py_ssize_t start, Py_ssize_t stop)
{
    Py_ssize_t i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);
    if (xa->nbits == 1)         /* faster for sparse bitarrays */
        return find_bit(self, GETBIT(xa, 0), start, stop);

    while (start <= stop - xa->nbits) {
        for (i = 0; i < xa->nbits; i++)
            if (GETBIT(self, start + i) != GETBIT(xa, i))
                goto next;

        return start;
    next:
        start++;
    }
    return -1;
}

static int
set_item(bitarrayobject *self, Py_ssize_t i, PyObject *v)
{
    int vi;

    assert(0 <= i && i < self->nbits);
    if ((vi = pybit_as_int(v)) < 0)
        return -1;
    setbit(self, i, vi);
    return 0;
}

static int
extend_bitarray(bitarrayobject *self, bitarrayobject *other)
{
    /* We have to store the sizes before we resize, and since
       other may be self, we also need to store other->nbits. */
    const Py_ssize_t self_nbits = self->nbits;
    const Py_ssize_t other_nbits = other->nbits;

    if (resize(self, self_nbits + other_nbits) < 0)
        return -1;

    copy_n(self, self_nbits, other, 0, other_nbits);
    return 0;
}

static int
extend_iter(bitarrayobject *self, PyObject *iter)
{
    const Py_ssize_t original_nbits = self->nbits;
    PyObject *item;

    assert(PyIter_Check(iter));
    while ((item = PyIter_Next(iter))) {
        if (resize(self, self->nbits + 1) < 0)
            goto error;
        if (set_item(self, self->nbits - 1, item) < 0)
            goto error;
        Py_DECREF(item);
    }
    if (PyErr_Occurred())
        return -1;

    return 0;
 error:
    Py_DECREF(item);
    resize(self, original_nbits);
    return -1;
}

static int
extend_sequence(bitarrayobject *self, PyObject *sequence)
{
    const Py_ssize_t original_nbits = self->nbits;
    PyObject *item;
    Py_ssize_t n, i;

    assert(PySequence_Check(sequence));
    n = PySequence_Size(sequence);

    if (resize(self, self->nbits + n) < 0)
        return -1;

    for (i = 0; i < n; i++) {
        item = PySequence_GetItem(sequence, i);
        if (item == NULL || set_item(self, self->nbits - n + i, item) < 0) {
            Py_XDECREF(item);
            resize(self, original_nbits);
            return -1;
        }
        Py_DECREF(item);
    }
    return 0;
}

static int
extend_bytes01(bitarrayobject *self, PyObject *bytes)
{
    const Py_ssize_t original_nbits = self->nbits;
    unsigned char c;
    char *data;
    int vi = 0;  /* to avoid uninitialized warning for some compilers */

    assert(PyBytes_Check(bytes));
    data = PyBytes_AS_STRING(bytes);

    while ((c = *data++)) {
        switch (c) {
        case '0': vi = 0; break;
        case '1': vi = 1; break;
        case ' ':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            continue;
        default:
            PyErr_Format(PyExc_ValueError, "expected '0' or '1' "
                         "(or whitespace), got '%c' (0x%02x)", c, c);
            resize(self, original_nbits);  /* no bits added on error */
            return -1;
        }
        if (resize(self, self->nbits + 1) < 0)
            return -1;
        setbit(self, self->nbits - 1, vi);
    }
    return 0;
}

static int
extend_unicode01(bitarrayobject *self, PyObject *unicode)
{
    PyObject *bytes;
    int res;

    assert(PyUnicode_Check(unicode));
    bytes = PyUnicode_AsASCIIString(unicode);
    if (bytes == NULL)
        return -1;

    assert(PyBytes_Check(bytes));
    res = extend_bytes01(self, bytes);
    Py_DECREF(bytes);  /* drop bytes */
    return res;
}

static int
extend_dispatch(bitarrayobject *self, PyObject *obj)
{
    PyObject *iter;

    /* dispatch on type */
    if (bitarray_Check(obj))                              /* bitarray */
        return extend_bitarray(self, (bitarrayobject *) obj);

    if (PyBytes_Check(obj)) {                             /* bytes 01 */
#ifdef IS_PY3K
        PyErr_SetString(PyExc_TypeError,
                        "cannot extend bitarray with 'bytes', "
                        "use .pack() or .frombytes() instead");
        return -1;
#else
        return extend_bytes01(self, obj);
#endif
    }

    if (PyUnicode_Check(obj))                           /* unicode 01 */
        return extend_unicode01(self, obj);

    if (PySequence_Check(obj))                            /* sequence */
        return extend_sequence(self, obj);

    if (PyIter_Check(obj))                                    /* iter */
        return extend_iter(self, obj);

    /* finally, try to get the iterator of the object */
    iter = PyObject_GetIter(obj);
    if (iter) {
        int res;

        res = extend_iter(self, iter);
        Py_DECREF(iter);
        return res;
    }

    PyErr_Format(PyExc_TypeError,
                 "'%s' object is not iterable", Py_TYPE(obj)->tp_name);
    return -1;
}

static PyObject *
unpack(bitarrayobject *self, char zero, char one, const char *fmt)
{
    PyObject *result;
    Py_ssize_t i;
    char *str;

    str = (char *) PyMem_Malloc((size_t) self->nbits);
    if (str == NULL)
        return PyErr_NoMemory();

    for (i = 0; i < self->nbits; i++)
        str[i] = GETBIT(self, i) ? one : zero;

    result = Py_BuildValue(fmt, str, self->nbits);
    PyMem_Free((void *) str);
    return result;
}

/* --------- helper functions not involving bitarrayobjects ------------ */

/* Normalize index (which may be negative), such that 0 <= i <= n */
static void
normalize_index(Py_ssize_t n, Py_ssize_t *i)
{
    if (*i < 0) {
        *i += n;
        if (*i < 0)
            *i = 0;
    }
    if (*i > n)
        *i = n;
}

/* Given a string, return an integer representing the endianness.
   If the string is invalid, set a Python exception and return -1. */
static int
endian_from_string(const char* string)
{
    assert(default_endian == ENDIAN_LITTLE || default_endian == ENDIAN_BIG);

    if (string == NULL)
        return default_endian;

    if (strcmp(string, "little") == 0)
        return ENDIAN_LITTLE;

    if (strcmp(string, "big") == 0)
        return ENDIAN_BIG;

    PyErr_Format(PyExc_ValueError, "bit endianness must be either "
                                   "'little' or 'big', got: '%s'", string);
    return -1;
}

/**************************************************************************
                     Implementation of bitarray methods
 **************************************************************************/

static PyObject *
bitarray_all(bitarrayobject *self)
{
    return PyBool_FromLong(find_bit(self, 0, 0, self->nbits) == -1);
}

PyDoc_STRVAR(all_doc,
"all() -> bool\n\
\n\
Return True when all bits in the array are True.\n\
Note that `a.all()` is faster than `all(a)`.");


static PyObject *
bitarray_any(bitarrayobject *self)
{
    return PyBool_FromLong(find_bit(self, 1, 0, self->nbits) >= 0);
}

PyDoc_STRVAR(any_doc,
"any() -> bool\n\
\n\
Return True when any bit in the array is True.\n\
Note that `a.any()` is faster than `any(a)`.");


static PyObject *
bitarray_append(bitarrayobject *self, PyObject *value)
{
    int vi;

    if ((vi = pybit_as_int(value)) < 0)
        return NULL;
    if (resize(self, self->nbits + 1) < 0)
        return NULL;
    setbit(self, self->nbits - 1, vi);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(append_doc,
"append(item, /)\n\
\n\
Append `item` to the end of the bitarray.");


static PyObject *
bitarray_bytereverse(bitarrayobject *self)
{
    bytereverse(self, 0, Py_SIZE(self));
    Py_RETURN_NONE;
}

PyDoc_STRVAR(bytereverse_doc,
"bytereverse()\n\
\n\
For all bytes representing the bitarray, reverse the bit order (in-place).\n\
Note: This method changes the actual machine values representing the\n\
bitarray; it does *not* change the endianness of the bitarray object.");


static PyObject *
bitarray_buffer_info(bitarrayobject *self)
{
    PyObject *res, *ptr;

    ptr = PyLong_FromVoidPtr(self->ob_item),
    res = Py_BuildValue("Onsin",
                        ptr,
                        Py_SIZE(self),
                        ENDIAN_STR(self),
                        (int) (BITS(Py_SIZE(self)) - self->nbits),
                        self->allocated);
    Py_DECREF(ptr);
    return res;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info() -> tuple\n\
\n\
Return a tuple (address, size, endianness, unused, allocated) giving the\n\
memory address of the bitarray's buffer, the buffer size (in bytes),\n\
the bit endianness as a string, the number of unused bits within the last\n\
byte, and the allocated memory for the buffer (in bytes).");


static PyObject *
bitarray_clear(bitarrayobject *self)
{
    if (resize(self, 0) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(clear_doc,
"clear()\n\
\n\
Remove all items from the bitarray.");


static PyObject *
bitarray_copy(bitarrayobject *self)
{
    PyObject *res;

    res = newbitarrayobject(Py_TYPE(self), self->nbits, self->endian);
    if (res == NULL)
        return NULL;

    memcpy(((bitarrayobject *) res)->ob_item, self->ob_item,
           (size_t) Py_SIZE(self));
    return res;
}

PyDoc_STRVAR(copy_doc,
"copy() -> bitarray\n\
\n\
Return a copy of the bitarray.");


static PyObject *
bitarray_count(bitarrayobject *self, PyObject *args)
{
    PyObject *v = Py_True;
    Py_ssize_t start = 0, stop = self->nbits;
    int vi;

    if (!PyArg_ParseTuple(args, "|Onn:count", &v, &start, &stop))
        return NULL;

    if ((vi = pybit_as_int(v)) < 0)
        return NULL;

    normalize_index(self->nbits, &start);
    normalize_index(self->nbits, &stop);

    return PyLong_FromSsize_t(count(self, vi, start, stop));
}

PyDoc_STRVAR(count_doc,
"count(value=1, start=0, stop=<end of array>, /) -> int\n\
\n\
Count the number of occurrences of `value` in the bitarray.");


static PyObject *
bitarray_endian(bitarrayobject *self)
{
    return Py_BuildValue("s", ENDIAN_STR(self));
}

PyDoc_STRVAR(endian_doc,
"endian() -> str\n\
\n\
Return the bit endianness of the bitarray as a string (`little` or `big`).");


static PyObject *
bitarray_extend(bitarrayobject *self, PyObject *obj)
{
    if (extend_dispatch(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(iterable, /)\n\
\n\
Append all the items from `iterable` to the end of the bitarray.\n\
If the iterable is a string, each `0` and `1` are appended as\n\
bits (ignoring whitespace).");


static PyObject *
bitarray_fill(bitarrayobject *self)
{
    long p;

    p = setunused(self);
    self->nbits += p;
    return PyLong_FromLong(p);
}

PyDoc_STRVAR(fill_doc,
"fill() -> int\n\
\n\
Add zeros to the end of the bitarray, such that the length of the bitarray\n\
will be a multiple of 8, and return the number of bits added (0..7).");


static PyObject *
bitarray_find(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t start = 0, stop = self->nbits;
    PyObject *x;

    if (!PyArg_ParseTuple(args, "O|nn", &x, &start, &stop))
        return NULL;

    normalize_index(self->nbits, &start);
    normalize_index(self->nbits, &stop);

    if (PyIndex_Check(x)) {
        int vi;

        if ((vi = pybit_as_int(x)) < 0)
            return NULL;
        return PyLong_FromSsize_t(find_bit(self, vi, start, stop));
    }

    if (bitarray_Check(x))
        return PyLong_FromSsize_t(
                    find(self, (bitarrayobject *) x, start, stop));

    PyErr_SetString(PyExc_TypeError, "bitarray or bool expected");
    return NULL;
}

PyDoc_STRVAR(find_doc,
"find(sub_bitarray, start=0, stop=<end of array>, /) -> int\n\
\n\
Return the lowest index where sub_bitarray is found, such that sub_bitarray\n\
is contained within `[start:stop]`.\n\
Return -1 when sub_bitarray is not found.");


static PyObject *
bitarray_index(bitarrayobject *self, PyObject *args)
{
    PyObject *ret;

    if ((ret = bitarray_find(self, args)) == NULL)
        return NULL;

    assert(PyLong_Check(ret));
    if (PyLong_AsSsize_t(ret) < 0) {
        Py_DECREF(ret);
#ifdef IS_PY3K
        return PyErr_Format(PyExc_ValueError, "%A not in bitarray",
                            PyTuple_GET_ITEM(args, 0));
#else
        PyErr_SetString(PyExc_ValueError, "item not in bitarray");
        return NULL;
#endif
    }
    return ret;
}

PyDoc_STRVAR(index_doc,
"index(sub_bitarray, start=0, stop=<end of array>, /) -> int\n\
\n\
Return the lowest index where sub_bitarray is found, such that sub_bitarray\n\
is contained within `[start:stop]`.\n\
Raises `ValueError` when the sub_bitarray is not present.");


static PyObject *
bitarray_insert(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i;
    PyObject *v;
    int vi;

    if (!PyArg_ParseTuple(args, "nO:insert", &i, &v))
        return NULL;

    normalize_index(self->nbits, &i);

    if ((vi = pybit_as_int(v)) < 0)
        return NULL;
    if (insert_n(self, i, 1) < 0)
        return NULL;
    setbit(self, i, vi);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(insert_doc,
"insert(index, value, /)\n\
\n\
Insert `value` into the bitarray before `index`.");


static PyObject *
bitarray_invert(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i = PY_SSIZE_T_MAX;

    if (!PyArg_ParseTuple(args, "|n:invert", &i))
        return NULL;

    if (i == PY_SSIZE_T_MAX) {  /* default - invert all bits */
        invert(self);
        Py_RETURN_NONE;
    }

    if (i < 0)
        i += self->nbits;

    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    setbit(self, i, 1 - GETBIT(self, i));
    Py_RETURN_NONE;
}

PyDoc_STRVAR(invert_doc,
"invert(index=<all bits>, /)\n\
\n\
Invert all bits in the array (in-place).\n\
When the optional `index` is given, only invert the single bit at index.");


static PyObject *
bitarray_reduce(bitarrayobject *self)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    PyObject *dict, *repr = NULL, *result = NULL;
    char *data;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }
    data = (char *) PyMem_Malloc(nbytes + 1);
    if (data == NULL) {
        PyErr_NoMemory();
        goto error;
    }
    /* first byte contains the number of unused bits */
    *data = (char) setunused(self);
    /* remaining bytes contain buffer */
    memcpy(data + 1, self->ob_item, (size_t) nbytes);
    repr = PyBytes_FromStringAndSize(data, nbytes + 1);
    if (repr == NULL)
        goto error;
    PyMem_Free((void *) data);
    result = Py_BuildValue("O(Os)O", Py_TYPE(self),
                           repr, ENDIAN_STR(self), dict);
 error:
    Py_DECREF(dict);
    Py_XDECREF(repr);
    return result;
}

PyDoc_STRVAR(reduce_doc, "state information for pickling");


static PyObject *
bitarray_repr(bitarrayobject *self)
{
    PyObject *result;
    Py_ssize_t i;
    char *str;
    size_t strsize;

    if (self->nbits == 0)
        return Py_BuildValue("s", "bitarray()");

    strsize = self->nbits + 12;  /* 12 is the length of "bitarray('')" */
    if (strsize > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError,
                        "bitarray too large to represent");
        return NULL;
    }

    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    /* add "bitarray('......')" to str */
    strcpy(str, "bitarray('"); /* has length 10 */
    /* don't use strcpy here, as this would add an extra NUL byte */
    str[strsize - 2] = '\'';
    str[strsize - 1] = ')';

    for (i = 0; i < self->nbits; i++)
        str[i + 10] = GETBIT(self, i) ? '1' : '0';

    result = Py_BuildValue("s#", str, (Py_ssize_t) strsize);
    PyMem_Free((void *) str);
    return result;
}


static PyObject *
bitarray_reverse(bitarrayobject *self)
{
    const Py_ssize_t m = self->nbits - 1;     /* index of last item */
    PyObject *t;       /* temp bitarray to store lower half of self */
    Py_ssize_t i;

    if (self->nbits < 2)        /* nothing to do */
        Py_RETURN_NONE;

    t = newbitarrayobject(Py_TYPE(self), self->nbits / 2, self->endian);
    if (t == NULL)
        return NULL;

#define tt  ((bitarrayobject *) t)
    /* copy lower half of array into temporary array */
    memcpy(tt->ob_item, self->ob_item, (size_t) Py_SIZE(tt));

    /* reverse upper half onto the lower half. */
    for (i = 0; i < tt->nbits; i++)
        setbit(self, i, GETBIT(self, m - i));

    /* reverse the stored away lower half onto the upper half of self. */
    for (i = 0; i < tt->nbits; i++)
        setbit(self, m - i, GETBIT(tt, i));
#undef tt
    Py_DECREF(t);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(reverse_doc,
"reverse()\n\
\n\
Reverse the order of bits in the array (in-place).");


static PyObject *
bitarray_search(bitarrayobject *self, PyObject *args)
{
    PyObject *list = NULL, *item = NULL, *t = NULL, *x;
    Py_ssize_t limit = PY_SSIZE_T_MAX, p = 0;

    if (!PyArg_ParseTuple(args, "O|n:search", &x, &limit))
        return NULL;

#define tt  ((bitarrayobject *) t)
    if (PyIndex_Check(x)) {
        int vi;

        if ((vi = pybit_as_int(x)) < 0)
            return NULL;
        if ((t = newbitarrayobject(Py_TYPE(self), 1, self->endian)) == NULL)
            return NULL;
        setbit(tt, 0, vi);
    }
    else if (bitarray_Check(x)) {
        t = x;
        Py_INCREF(t);
    }
    else {
        PyErr_SetString(PyExc_TypeError, "bitarray or bool expected");
        return NULL;
    }

    if (tt->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        goto error;
    }
    if ((list = PyList_New(0)) == NULL)
        goto error;

    while ((p = find(self, tt, p, self->nbits)) >= 0) {
        if (PyList_Size(list) >= limit)
            break;
        item = PyLong_FromSsize_t(p++);
        if (item == NULL || PyList_Append(list, item) < 0)
            goto error;
        Py_DECREF(item);
    }
#undef tt
    Py_DECREF(t);
    return list;

 error:
    Py_XDECREF(item);
    Py_XDECREF(list);
    Py_DECREF(t);
    return NULL;
}

PyDoc_STRVAR(search_doc,
"search(sub_bitarray, limit=<none>, /) -> list\n\
\n\
Searches for the given sub_bitarray in self, and return the list of start\n\
positions.\n\
The optional argument limits the number of search results to the integer\n\
specified.  By default, all search results are returned.");


static PyObject *
bitarray_setall(bitarrayobject *self, PyObject *v)
{
    int vi;

    if ((vi = pybit_as_int(v)) < 0)
        return NULL;
    memset(self->ob_item, vi ? 0xff : 0x00, (size_t) Py_SIZE(self));
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setall_doc,
"setall(value, /)\n\
\n\
Set all elements in the bitarray to `value`.\n\
Note that `a.setall(value)` is equivalent to `a[:] = value`.");


static PyObject *
bitarray_sort(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"reverse", NULL};
    const Py_ssize_t n = self->nbits;
    Py_ssize_t n0, n1;
    int reverse = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    n1 = count(self, 1, 0, n);

    if (reverse) {
        setrange(self, 0, n1, 1);
        setrange(self, n1, n, 0);
    }
    else {
        n0 = n - n1;
        setrange(self, 0, n0, 0);
        setrange(self, n0, n, 1);
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(sort_doc,
"sort(reverse=False)\n\
\n\
Sort the bits in the array (in-place).");


static PyObject *
bitarray_tolist(bitarrayobject *self)
{
    PyObject *list, *item;
    Py_ssize_t i;

    list = PyList_New(self->nbits);
    if (list == NULL)
        return NULL;

    for (i = 0; i < self->nbits; i++) {
        item = PyLong_FromLong(GETBIT(self, i));
        if (item == NULL)
            return NULL;
        if (PyList_SetItem(list, i, item) < 0)
            return NULL;
    }
    return list;
}

PyDoc_STRVAR(tolist_doc,
"tolist() -> list\n\
\n\
Return a list with the items (0 or 1) in the bitarray.\n\
Note that the list object being created will require 32 or 64 times more\n\
memory (depending on the machine architecture) than the bitarray object,\n\
which may cause a memory error if the bitarray is very large.");


static PyObject *
bitarray_frombytes(bitarrayobject *self, PyObject *bytes)
{
    Py_ssize_t nbytes;
    Py_ssize_t t, p;

    if (!PyBytes_Check(bytes))
        return PyErr_Format(PyExc_TypeError, "bytes expected, not %s",
                            Py_TYPE(bytes)->tp_name);

    nbytes = PyBytes_GET_SIZE(bytes);
    if (nbytes == 0)
        Py_RETURN_NONE;

    /* Before we extend the raw bytes with the new data, we need to store
       the current size and pad the last byte, as our bitarray size might
       not be a multiple of 8.  After extending, we remove the padding
       bits again.
    */
    t = self->nbits;
    p = setunused(self);
    self->nbits += p;
    assert(self->nbits % 8 == 0);

    if (resize(self, self->nbits + BITS(nbytes)) < 0)
        return NULL;

    memcpy(self->ob_item + (Py_SIZE(self) - nbytes),
           PyBytes_AS_STRING(bytes), (size_t) nbytes);

    if (p && delete_n(self, t, p) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(frombytes_doc,
"frombytes(bytes, /)\n\
\n\
Extend bitarray with raw bytes.  That is, each append byte will add eight\n\
bits to the bitarray.");


static PyObject *
bitarray_tobytes(bitarrayobject *self)
{
    setunused(self);
    return PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
}

PyDoc_STRVAR(tobytes_doc,
"tobytes() -> bytes\n\
\n\
Return the byte representation of the bitarray.\n\
When the length of the bitarray is not a multiple of 8, the few remaining\n\
bits are considered 0.");


static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *bytes, *f, *res;
    Py_ssize_t nblock, nread = 0, nbytes = -1;
    int not_enough_bytes;

    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &nbytes))
        return NULL;

    if (nbytes < 0)  /* read till EOF */
        nbytes = PY_SSIZE_T_MAX;

    while (nread < nbytes) {
        nblock = Py_MIN(nbytes - nread, BLOCKSIZE);
        bytes = PyObject_CallMethod(f, "read", "n", nblock);
        if (bytes == NULL)
            return NULL;
        if (!PyBytes_Check(bytes)) {
            Py_DECREF(bytes);
            PyErr_SetString(PyExc_TypeError, "read() didn't return bytes");
            return NULL;
        }
        not_enough_bytes = (PyBytes_GET_SIZE(bytes) < nblock);
        nread += PyBytes_GET_SIZE(bytes);
        assert(nread >= 0 && nread <= nbytes);

        res = bitarray_frombytes(self, bytes);
        Py_DECREF(bytes);
        if (res == NULL)
            return NULL;
        Py_DECREF(res);  /* drop frombytes result */

        if (not_enough_bytes) {
            if (nbytes == PY_SSIZE_T_MAX)  /* read till EOF */
                break;
            PyErr_SetString(PyExc_EOFError, "not enough bytes to read");
            return NULL;
        }
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(fromfile_doc,
"fromfile(f, n=-1, /)\n\
\n\
Extend bitarray with up to n bytes read from the file object f.\n\
When n is omitted or negative, reads all data until EOF.\n\
When n is provided and positive but exceeds the data available,\n\
`EOFError` is raised (but the available data is still read and appended.");


static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    Py_ssize_t size, offset;
    PyObject *res;

    setunused(self);
    for (offset = 0; offset < nbytes; offset += BLOCKSIZE) {
        size = Py_MIN(nbytes - offset, BLOCKSIZE);
        assert(size >= 0 && offset + size <= nbytes);
        /* basically: f.write(memoryview(self)[offset:offset + size] */
        res = PyObject_CallMethod(f, "write", BYTES_SIZE_FMT,
                                  self->ob_item + offset, size);
        if (res == NULL)
            return NULL;
        Py_DECREF(res);  /* drop write result */
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(tofile_doc,
"tofile(f, /)\n\
\n\
Write the byte representation of the bitarray to the file object f.\n\
When the length of the bitarray is not a multiple of 8, the few remaining\n\
bits are considered 0.");


static PyObject *
bitarray_to01(bitarrayobject *self)
{
    return unpack(self, '0', '1', "s#");
}

PyDoc_STRVAR(to01_doc,
"to01() -> str\n\
\n\
Return a string containing '0's and '1's, representing the bits in the\n\
bitarray object.");


static PyObject *
bitarray_unpack(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"zero", "one", NULL};
    char zero = 0x00, one = 0x01;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    return unpack(self, zero, one, BYTES_SIZE_FMT);
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\x01') -> bytes\n\
\n\
Return bytes containing one character for each bit in the bitarray,\n\
using the specified mapping.");


static PyObject *
bitarray_pack(bitarrayobject *self, PyObject *bytes)
{
    Py_ssize_t nbytes, i;
    char *data;

    if (!PyBytes_Check(bytes))
        return PyErr_Format(PyExc_TypeError, "bytes expected, not %s",
                            Py_TYPE(bytes)->tp_name);

    nbytes = PyBytes_GET_SIZE(bytes);

    if (resize(self, self->nbits + nbytes) < 0)
        return NULL;

    data = PyBytes_AS_STRING(bytes);
    for (i = 0; i < nbytes; i++)
        setbit(self, self->nbits - nbytes + i, data[i] ? 1 : 0);

    Py_RETURN_NONE;
}

PyDoc_STRVAR(pack_doc,
"pack(bytes, /)\n\
\n\
Extend the bitarray from bytes, where each byte corresponds to a single\n\
bit.  The byte `b'\\x00'` maps to bit 0 and all other characters map to\n\
bit 1.\n\
This method, as well as the unpack method, are meant for efficient\n\
transfer of data between bitarray objects to other python objects\n\
(for example NumPy's ndarray object) which have a different memory view.");


static PyObject *
bitarray_pop(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i = -1;
    long vi;

    if (!PyArg_ParseTuple(args, "|n:pop", &i))
        return NULL;

    if (self->nbits == 0) {
        /* special case -- most common failure cause */
        PyErr_SetString(PyExc_IndexError, "pop from empty bitarray");
        return NULL;
    }
    if (i < 0)
        i += self->nbits;

    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError, "pop index out of range");
        return NULL;
    }
    vi = GETBIT(self, i);
    if (delete_n(self, i, 1) < 0)
        return NULL;
    return PyLong_FromLong(vi);
}

PyDoc_STRVAR(pop_doc,
"pop(index=-1, /) -> item\n\
\n\
Return the i-th (default last) element and delete it from the bitarray.\n\
Raises `IndexError` if bitarray is empty or index is out of range.");


static PyObject *
bitarray_remove(bitarrayobject *self, PyObject *value)
{
    Py_ssize_t i;
    int vi;

    if ((vi = pybit_as_int(value)) < 0)
        return NULL;

    if ((i = find_bit(self, vi, 0, self->nbits)) < 0)
        return PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);

    if (delete_n(self, i, 1) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(remove_doc,
"remove(value, /)\n\
\n\
Remove the first occurrence of `value` in the bitarray.\n\
Raises `ValueError` if item is not present.");


static PyObject *
bitarray_sizeof(bitarrayobject *self)
{
    Py_ssize_t res;

    res = sizeof(bitarrayobject) + self->allocated;
    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof_doc,
"Return the size of the bitarray in memory, in bytes.");


/* ----------------------- bitarray_as_sequence ------------------------ */

static Py_ssize_t
bitarray_len(bitarrayobject *self)
{
    return self->nbits;
}

static PyObject *
bitarray_concat(bitarrayobject *self, PyObject *other)
{
    PyObject *res;

    if ((res = bitarray_copy(self)) == NULL)
        return NULL;

    if (extend_dispatch((bitarrayobject *) res, other) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

static PyObject *
bitarray_repeat(bitarrayobject *self, Py_ssize_t n)
{
    PyObject *res;

    if ((res = bitarray_copy(self)) == NULL)
        return NULL;

    if (repeat((bitarrayobject *) res, n) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

static PyObject *
bitarray_item(bitarrayobject *self, Py_ssize_t i)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
        return NULL;
    }
    return PyLong_FromLong(GETBIT(self, i));
}

static int
bitarray_ass_item(bitarrayobject *self, Py_ssize_t i, PyObject *value)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError,
                        "bitarray assignment index out of range");
        return -1;
    }
    if (value == NULL)
        return delete_n(self, i, 1);
    else
        return set_item(self, i, value);
}

/* return 1 if value (which can be an int or bitarray) is in self,
   0 otherwise, and -1 on error */
static int
bitarray_contains(bitarrayobject *self, PyObject *value)
{
    if (PyIndex_Check(value)) {
        int vi;

        vi = pybit_as_int(value);
        if (vi < 0)
            return -1;
        return find_bit(self, vi, 0, self->nbits) >= 0;
    }

    if (bitarray_Check(value))
        return find(self, (bitarrayobject *) value, 0, self->nbits) >= 0;

    PyErr_Format(PyExc_TypeError, "bitarray or bool expected, got %s",
                 Py_TYPE(value)->tp_name);
    return -1;
}

static PyObject *
bitarray_inplace_concat(bitarrayobject *self, PyObject *other)
{
    if (extend_dispatch(self, other) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
bitarray_inplace_repeat(bitarrayobject *self, Py_ssize_t n)
{
    if (repeat(self, n) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PySequenceMethods bitarray_as_sequence = {
    (lenfunc) bitarray_len,                     /* sq_length */
    (binaryfunc) bitarray_concat,               /* sq_concat */
    (ssizeargfunc) bitarray_repeat,             /* sq_repeat */
    (ssizeargfunc) bitarray_item,               /* sq_item */
    0,                                          /* sq_slice */
    (ssizeobjargproc) bitarray_ass_item,        /* sq_ass_item */
    0,                                          /* sq_ass_slice */
    (objobjproc) bitarray_contains,             /* sq_contains */
    (binaryfunc) bitarray_inplace_concat,       /* sq_inplace_concat */
    (ssizeargfunc) bitarray_inplace_repeat,     /* sq_inplace_repeat */
};

/* ----------------------- bitarray_as_mapping ------------------------- */

static PyObject *
bitarray_subscr(bitarrayobject *self, PyObject *item)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0)
            i += self->nbits;
        return bitarray_item(self, i);
    }

    if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength, i, j;
        PyObject *res;

        if (PySlice_GetIndicesEx(item, self->nbits,
                                 &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }
        res = newbitarrayobject(Py_TYPE(self), slicelength, self->endian);
        if (res == NULL)
            return NULL;

        if (step == 1) {
            copy_n((bitarrayobject *) res, 0, self, start, slicelength);
        }
        else {
            for (i = 0, j = start; i < slicelength; i++, j += step)
                setbit((bitarrayobject *) res, i, GETBIT(self, j));
        }
        return res;
    }

    return PyErr_Format(PyExc_TypeError,
                        "bitarray indices must be integers or slices, not %s",
                        Py_TYPE(item)->tp_name);
}

/* The following functions (setslice_bitarray, setslice_bool and delslice)
   are called from bitarray_ass_subscr.  Having this functionality inside
   bitarray_ass_subscr would make the function incomprehensibly long. */

/* set the elements in self, specified by slice, to bitarray */
static int
setslice_bitarray(bitarrayobject *self, PyObject *slice, PyObject *array)
{
    Py_ssize_t start, stop, step, slicelength, increase, i, j;
    int copy_array = 0, res = -1;

    assert(PySlice_Check(slice) && bitarray_Check(array));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;

#define aa  ((bitarrayobject *) array)
    /* number of bits by which 'self' has to be increased (decreased) */
    increase = aa->nbits - slicelength;

    if (aa == self) {  /* covers cases like a[2::] = a and a[::-1] = a */
        if ((array = bitarray_copy(aa)) == NULL)
            return -1;
        copy_array = 1;
    }

    if (step == 1) {
        if (increase > 0) {        /* increase self */
            if (insert_n(self, start, increase) < 0)
                goto error;
        }
        if (increase < 0) {        /* decrease self */
            if (delete_n(self, start, -increase) < 0)
                goto error;
        }
        /* copy the new values into self */
        copy_n(self, start, aa, 0, aa->nbits);
    }
    else {  /* step != 1 */
        if (increase != 0) {
            PyErr_Format(PyExc_ValueError,
                         "attempt to assign sequence of size %zd "
                         "to extended slice of size %zd",
                         aa->nbits, slicelength);
            goto error;
        }
        assert(increase == 0);
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, GETBIT(aa, i));
    }
#undef aa

    res = 0;
 error:
    if (copy_array)
        Py_DECREF(array);
    return res;
}

/* set the elements in self, specified by slice, to value */
static int
setslice_bool(bitarrayobject *self, PyObject *slice, PyObject *value)
{
    Py_ssize_t start, stop, step, slicelength, i, j;
    int vi;

    assert(PySlice_Check(slice) && PyIndex_Check(value));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;

    if ((vi = pybit_as_int(value)) < 0)
        return -1;

    if (step == 1) {
        setrange(self, start, start + slicelength, vi);
    }
    else {  /* step != 1 */
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, vi);
    }
    return 0;
}

/* delete the elements in self, specified by slice */
static int
delslice(bitarrayobject *self, PyObject *slice)
{
    Py_ssize_t start, stop, step, slicelength;

    assert(PySlice_Check(slice));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;

    if (slicelength == 0)
        return 0;

    if (step < 0) {
        stop = start + 1;
        start = stop + step * (slicelength - 1) - 1;
        step = -step;
    }
    assert(step > 0 && start <= stop && slicelength > 0);
    assert(0 <= start && start < self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (step == 1) {
        assert(stop - start == slicelength);
        return delete_n(self, start, slicelength);
    }
    else {
        Py_ssize_t i, j;
        /* Now step > 1.  We set the items not to be removed. */
        for (i = j = start; i < self->nbits; i++) {
            if ((i - start) % step != 0 || i >= stop)
                setbit(self, j++, GETBIT(self, i));
        }
        return resize(self, self->nbits - slicelength);
    }
}

static int
bitarray_ass_subscr(bitarrayobject *self, PyObject* item, PyObject* value)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;
        if (i < 0)
            i += self->nbits;
        return bitarray_ass_item(self, i, value);
    }

    if (PySlice_Check(item)) {
        if (value == NULL)
            return delslice(self, item);

        if (bitarray_Check(value))
            return setslice_bitarray(self, item, value);

        if (PyIndex_Check(value))
            return setslice_bool(self, item, value);

        PyErr_Format(PyExc_TypeError,
                     "bitarray or bool expected for slice assignment, not %s",
                     Py_TYPE(value)->tp_name);
        return -1;
    }

    PyErr_Format(PyExc_TypeError,
                 "bitarray indices must be integers or slices, not %s",
                 Py_TYPE(item)->tp_name);
    return -1;
}

static PyMappingMethods bitarray_as_mapping = {
    (lenfunc) bitarray_len,
    (binaryfunc) bitarray_subscr,
    (objobjargproc) bitarray_ass_subscr,
};

/* --------------------------- bitarray_as_number ---------------------- */

static PyObject *
bitarray_cpinvert(bitarrayobject *self)
{
    PyObject *result;

    if ((result = bitarray_copy(self)) == NULL)
        return NULL;

    invert((bitarrayobject *) result);
    return result;
}

enum op_type {
    OP_and,
    OP_or,
    OP_xor,
};

/* perform bitwise in-place operation */
static void
bitwise(bitarrayobject *self, bitarrayobject *other, enum op_type oper)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    Py_ssize_t i;

    assert(self->nbits == other->nbits);
    assert(self->endian == other->endian);
    switch (oper) {
    case OP_and:
        for (i = 0; i < nbytes; i++)
            self->ob_item[i] &= other->ob_item[i];
        break;
    case OP_or:
        for (i = 0; i < nbytes; i++)
            self->ob_item[i] |= other->ob_item[i];
        break;
    case OP_xor:
        for (i = 0; i < nbytes; i++)
            self->ob_item[i] ^= other->ob_item[i];
        break;
    default:                    /* cannot happen */
        Py_FatalError("unknown bitwise operation");
    }
}

/* return 0 if both a and b are bitarray objects, -1 on error */
static int
bitwise_check(PyObject *a, PyObject *b, const char *ostr)
{
    if (!bitarray_Check(a) || !bitarray_Check(b)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
        return -1;
    }
#define aa  ((bitarrayobject *) a)
#define bb  ((bitarrayobject *) b)
    if (aa->nbits != bb->nbits) {
        PyErr_Format(PyExc_ValueError,
                     "bitarrays of equal length expected for '%s'", ostr);
        return -1;
    }
    if (aa->endian != bb->endian) {
        PyErr_Format(PyExc_ValueError,
                     "bitarrays of equal endianness expected for '%s'", ostr);
        return -1;
    }
#undef aa
#undef bb
    return 0;
}

#define BITWISE_FUNC(oper, ostr)                             \
static PyObject *                                            \
bitarray_ ## oper (PyObject *self, PyObject *other)          \
{                                                            \
    PyObject *res;                                           \
                                                             \
    if (bitwise_check(self, other, ostr) < 0)                \
        return NULL;                                         \
    res = bitarray_copy((bitarrayobject *) self);            \
    if (res == NULL)                                         \
        return NULL;                                         \
    bitwise((bitarrayobject *) res,                          \
            (bitarrayobject *) other, OP_ ## oper);          \
    return res;                                              \
}

BITWISE_FUNC(and, "&")               /* bitarray_and */
BITWISE_FUNC(or,  "|")               /* bitarray_or  */
BITWISE_FUNC(xor, "^")               /* bitarray_xor */


#define BITWISE_IFUNC(oper, ostr)                            \
static PyObject *                                            \
bitarray_i ## oper (PyObject *self, PyObject *other)         \
{                                                            \
    if (bitwise_check(self, other, ostr) < 0)                \
        return NULL;                                         \
    bitwise((bitarrayobject *) self,                         \
            (bitarrayobject *) other, OP_ ## oper);          \
    Py_INCREF(self);                                         \
    return self;                                             \
}

BITWISE_IFUNC(and, "&=")             /* bitarray_iand */
BITWISE_IFUNC(or,  "|=")             /* bitarray_ior  */
BITWISE_IFUNC(xor, "^=")             /* bitarray_ixor */


/* shift bitarray n positions to left (right=0) or right (right=1) */
static void
shift(bitarrayobject *self, Py_ssize_t n, int right)
{
    Py_ssize_t nbits = self->nbits;

    if (n == 0)
        return;

    if (n >= nbits) {
        memset(self->ob_item, 0x00, (size_t) Py_SIZE(self));
        return;
    }

    assert(0 < n && n < nbits);
    if (right) {                /* rshift */
        copy_n(self, n, self, 0, nbits - n);
        setrange(self, 0, n, 0);
    }
    else {                      /* lshift */
        copy_n(self, 0, self, n, nbits - n);
        setrange(self, nbits - n, nbits, 0);
    }
}

/* check shift arguments and return the shift count, -1 on error */
static Py_ssize_t
shift_check(PyObject *a, PyObject *b, const char *ostr)
{
    Py_ssize_t n;

    if (!bitarray_Check(a) || !PyIndex_Check(b)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
        return -1;
    }
    n = PyNumber_AsSsize_t(b, PyExc_OverflowError);
    if (n == -1 && PyErr_Occurred())
        return -1;

    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return -1;
    }
    return n;
}

#define SHIFT_FUNC(name, inplace, right, ostr)         \
static PyObject *                                      \
bitarray_ ## name (PyObject *self, PyObject *other)    \
{                                                      \
    PyObject *res;                                     \
    Py_ssize_t n;                                      \
                                                       \
    if ((n = shift_check(self, other, ostr)) < 0)      \
        return NULL;                                   \
    if (inplace) {                                     \
        res = self;                                    \
        Py_INCREF(res);                                \
    }                                                  \
    else {                                             \
        res = bitarray_copy((bitarrayobject *) self);  \
        if (res == NULL)                               \
            return NULL;                               \
    }                                                  \
    shift((bitarrayobject *) res, n, right);           \
    return res;                                        \
}

SHIFT_FUNC(lshift,  0, 0, "<<")  /* bitarray_lshift */
SHIFT_FUNC(rshift,  0, 1, ">>")  /* bitarray_rshift */
SHIFT_FUNC(ilshift, 1, 0, "<<=") /* bitarray_ilshift */
SHIFT_FUNC(irshift, 1, 1, ">>=") /* bitarray_irshift */


static PyNumberMethods bitarray_as_number = {
    0,                                   /* nb_add */
    0,                                   /* nb_subtract */
    0,                                   /* nb_multiply */
#if PY_MAJOR_VERSION == 2
    0,                                   /* nb_divide */
#endif
    0,                                   /* nb_remainder */
    0,                                   /* nb_divmod */
    0,                                   /* nb_power */
    0,                                   /* nb_negative */
    0,                                   /* nb_positive */
    0,                                   /* nb_absolute */
    0,                                   /* nb_bool (was nb_nonzero) */
    (unaryfunc) bitarray_cpinvert,       /* nb_invert */
    (binaryfunc) bitarray_lshift,        /* nb_lshift */
    (binaryfunc) bitarray_rshift,        /* nb_rshift */
    (binaryfunc) bitarray_and,           /* nb_and */
    (binaryfunc) bitarray_xor,           /* nb_xor */
    (binaryfunc) bitarray_or,            /* nb_or */
#if PY_MAJOR_VERSION == 2
    0,                                   /* nb_coerce */
#endif
    0,                                   /* nb_int */
    0,                                   /* nb_reserved (was nb_long) */
    0,                                   /* nb_float */
#if PY_MAJOR_VERSION == 2
    0,                                   /* nb_oct */
    0,                                   /* nb_hex */
#endif
    0,                                   /* nb_inplace_add */
    0,                                   /* nb_inplace_subtract */
    0,                                   /* nb_inplace_multiply */
#if PY_MAJOR_VERSION == 2
    0,                                   /* nb_inplace_divide */
#endif
    0,                                   /* nb_inplace_remainder */
    0,                                   /* nb_inplace_power */
    (binaryfunc) bitarray_ilshift,       /* nb_inplace_lshift */
    (binaryfunc) bitarray_irshift,       /* nb_inplace_rshift */
    (binaryfunc) bitarray_iand,          /* nb_inplace_and */
    (binaryfunc) bitarray_ixor,          /* nb_inplace_xor */
    (binaryfunc) bitarray_ior,           /* nb_inplace_or */
    0,                                   /* nb_floor_divide */
    0,                                   /* nb_true_divide */
    0,                                   /* nb_inplace_floor_divide */
    0,                                   /* nb_inplace_true_divide */
#if PY_MAJOR_VERSION == 3
    0,                                   /* nb_index */
#endif
};

/**************************************************************************
                    variable length encoding and decoding
 **************************************************************************/

static int
check_codedict(PyObject *codedict)
{
    if (!PyDict_Check(codedict)) {
        PyErr_Format(PyExc_TypeError, "dict expected, got %s",
                     Py_TYPE(codedict)->tp_name);
        return -1;
    }
    if (PyDict_Size(codedict) == 0) {
        PyErr_SetString(PyExc_ValueError, "non-empty dict expected");
        return -1;
    }
    return 0;
}

static int
check_value(PyObject *value)
{
     if (!bitarray_Check(value)) {
         PyErr_SetString(PyExc_TypeError,
                         "bitarray expected for dict value");
         return -1;
     }
     if (((bitarrayobject *) value)->nbits == 0) {
         PyErr_SetString(PyExc_ValueError, "non-empty bitarray expected");
         return -1;
     }
     return 0;
}

static PyObject *
bitarray_encode(bitarrayobject *self, PyObject *args)
{
    PyObject *codedict, *iterable, *iter, *symbol, *value;

    if (!PyArg_ParseTuple(args, "OO:encode", &codedict, &iterable))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    iter = PyObject_GetIter(iterable);
    if (iter == NULL)
        return PyErr_Format(PyExc_TypeError, "'%s' object is not iterable",
                            Py_TYPE(iterable)->tp_name);

    /* extend self with the bitarrays from codedict */
    while ((symbol = PyIter_Next(iter))) {
        value = PyDict_GetItem(codedict, symbol);
        Py_DECREF(symbol);
        if (value == NULL) {
#ifdef IS_PY3K
            PyErr_Format(PyExc_ValueError,
                         "symbol not defined in prefix code: %A", symbol);
#else
            PyErr_SetString(PyExc_ValueError,
                            "symbol not defined in prefix code");
#endif
            goto error;
        }
        if (check_value(value) < 0 ||
                extend_bitarray(self, (bitarrayobject *) value) < 0)
            goto error;
    }
    Py_DECREF(iter);
    if (PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;

 error:
    Py_DECREF(iter);
    return NULL;
}

PyDoc_STRVAR(encode_doc,
"encode(code, iterable, /)\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
iterate over the iterable object with symbols, and extend the bitarray\n\
with the corresponding bitarray for each symbol.");

/* ----------------------- binary tree (C-level) ----------------------- */

/* a node has either children or a symbol, NEVER both */
typedef struct _bin_node
{
    struct _bin_node *child[2];
    PyObject *symbol;
} binode;


static binode *
binode_new(void)
{
    binode *nd;

    nd = (binode *) PyMem_Malloc(sizeof(binode));
    if (nd == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    nd->child[0] = NULL;
    nd->child[1] = NULL;
    nd->symbol = NULL;
    return nd;
}

static void
binode_delete(binode *nd)
{
    if (nd == NULL)
        return;

    binode_delete(nd->child[0]);
    binode_delete(nd->child[1]);
    Py_XDECREF(nd->symbol);
    PyMem_Free(nd);
}

/* insert symbol (mapping to ba) into the tree */
static int
binode_insert_symbol(binode *tree, bitarrayobject *ba, PyObject *symbol)
{
    binode *nd = tree, *prev;
    Py_ssize_t i;
    int k;

    for (i = 0; i < ba->nbits; i++) {
        k = GETBIT(ba, i);
        prev = nd;
        nd = nd->child[k];

        if (nd) {
            if (nd->symbol)     /* we cannot have already a symbol */
                goto ambiguity;
        }
        else {            /* if node does not exist, create new one */
            nd = binode_new();
            if (nd == NULL)
                return -1;
            prev->child[k] = nd;
        }
    }
    /* the new leaf node cannot already have a symbol or children */
    if (nd->symbol || nd->child[0] || nd->child[1])
        goto ambiguity;

    nd->symbol = symbol;
    Py_INCREF(symbol);
    return 0;

 ambiguity:
#ifdef IS_PY3K
    PyErr_Format(PyExc_ValueError, "prefix code ambiguous: %A", symbol);
#else
    PyErr_SetString(PyExc_ValueError, "prefix code ambiguous");
#endif
    return -1;
}

/* return a binary tree from a codedict, which is created by inserting
   all symbols mapping to bitarrays */
static binode *
binode_make_tree(PyObject *codedict)
{
    binode *tree;
    PyObject *symbol, *value;
    Py_ssize_t pos = 0;

    tree = binode_new();
    if (tree == NULL)
        return NULL;

    while (PyDict_Next(codedict, &pos, &symbol, &value)) {
        if (check_value(value) < 0 ||
            binode_insert_symbol(tree, (bitarrayobject *) value, symbol) < 0)
            {
                binode_delete(tree);
                return NULL;
            }
    }
    /* as we require the codedict to be non-empty the tree cannot be empty */
    assert(tree);
    return tree;
}

/* Traverse using the branches corresponding to bits in `ba`, starting
   at *indexp.  Return the symbol at the leaf node, or NULL when the end
   of the bitarray has been reached.  On error, NULL is also returned,
   and the appropriate exception is set.
*/
static PyObject *
binode_traverse(binode *tree, bitarrayobject *ba, Py_ssize_t *indexp)
{
    binode *nd = tree;
    Py_ssize_t start = *indexp;

    while (*indexp < ba->nbits) {
        assert(nd);
        nd = nd->child[GETBIT(ba, *indexp)];
        if (nd == NULL)
            return PyErr_Format(PyExc_ValueError,
                                "prefix code unrecognized in bitarray "
                                "at position %zd .. %zd", start, *indexp);
        (*indexp)++;
        if (nd->symbol) {       /* leaf */
            assert(nd->child[0] == NULL && nd->child[1] == NULL);
            return nd->symbol;
        }
    }
    if (nd != tree)
        PyErr_Format(PyExc_ValueError,
                     "incomplete prefix code at position %zd", start);
    return NULL;
}

/* add the node's symbol to given dict */
static int
binode_to_dict(binode *nd, PyObject *dict, bitarrayobject *prefix)
{
    bitarrayobject *t;          /* prefix of the two child nodes */
    int k, ret;

    if (nd == NULL)
        return 0;

    if (nd->symbol) {
        assert(nd->child[0] == NULL && nd->child[1] == NULL);
        if (PyDict_SetItem(dict, nd->symbol, (PyObject *) prefix) < 0)
            return -1;
        return 0;
    }

    for (k = 0; k < 2; k++) {
        t = (bitarrayobject *) bitarray_copy(prefix);
        if (t == NULL)
            return -1;
        if (resize(t, t->nbits + 1) < 0)
            return -1;
        setbit(t, t->nbits - 1, k);
        ret = binode_to_dict(nd->child[k], dict, t);
        Py_DECREF((PyObject *) t);
        if (ret < 0)
            return -1;
    }
    return 0;
}

/* return the number of nodes */
static Py_ssize_t
binode_nodes(binode *nd)
{
    Py_ssize_t res;

    if (nd == NULL)
        return 0;

    /* a node cannot have a symbol and children */
    assert(!(nd->symbol && (nd->child[0] || nd->child[1])));
    /* a node must have a symbol or children */
    assert(nd->symbol || nd->child[0] || nd->child[1]);

    res = 1;
    res += binode_nodes(nd->child[0]);
    res += binode_nodes(nd->child[1]);
    return res;
}

/******************************** decodetree ******************************/

typedef struct {
    PyObject_HEAD
    binode *tree;
} decodetreeobject;


static PyObject *
decodetree_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    binode *tree;
    PyObject *codedict;
    decodetreeobject *self;

    if (!PyArg_ParseTuple(args, "O:decodetree", &codedict))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = binode_make_tree(codedict);
    if (tree == NULL)
        return NULL;

    self = (decodetreeobject *) type->tp_alloc(type, 0);
    if (self == NULL) {
        binode_delete(tree);
        return NULL;
    }
    self->tree = tree;

    return (PyObject *) self;
}

/* Return a dict mapping the symbols to bitarrays.  This dict is a
   reconstruction of the code dict the decodetree was created with. */
static PyObject *
decodetree_todict(decodetreeobject *self)
{
    PyObject *dict, *prefix;

    if ((dict = PyDict_New()) == NULL)
        return NULL;

    prefix = newbitarrayobject(&Bitarray_Type, 0, default_endian);
    if (prefix == NULL)
        goto error;

    if (binode_to_dict(self->tree, dict, (bitarrayobject *) prefix) < 0)
        goto error;

    Py_DECREF(prefix);
    return dict;

 error:
    Py_DECREF(dict);
    Py_XDECREF(prefix);
    return NULL;
}

/* Return the number of nodes in the tree (not just symbols) */
static PyObject *
decodetree_nodes(decodetreeobject *self)
{
    return PyLong_FromSsize_t(binode_nodes(self->tree));
}

static PyObject *
decodetree_sizeof(decodetreeobject *self)
{
    Py_ssize_t res;

    res = sizeof(decodetreeobject);
    res += sizeof(binode) * binode_nodes(self->tree);
    return PyLong_FromSsize_t(res);
}

static void
decodetree_dealloc(decodetreeobject *self)
{
    binode_delete(self->tree);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* as these methods are only useful for debugging and testing,
   they are only documented within this file */
static PyMethodDef decodetree_methods[] = {
    {"nodes",       (PyCFunction) decodetree_nodes,   METH_NOARGS, 0},
    {"todict",      (PyCFunction) decodetree_todict,  METH_NOARGS, 0},
    {"__sizeof__",  (PyCFunction) decodetree_sizeof,  METH_NOARGS, 0},
    {NULL,          NULL}  /* sentinel */
};

PyDoc_STRVAR(decodetree_doc,
"decodetree(code, /) -> decodetree\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
create a binary tree object to be passed to `.decode()` or `.iterdecode()`.");

static PyTypeObject DecodeTree_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.decodetree",                    /* tp_name */
    sizeof(decodetreeobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) decodetree_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number*/
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                       /* tp_flags */
    decodetree_doc,                           /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    decodetree_methods,                       /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    PyType_GenericAlloc,                      /* tp_alloc */
    decodetree_new,                           /* tp_new */
    PyObject_Del,                             /* tp_free */
};

#define DecodeTree_Check(op)  PyObject_TypeCheck(op, &DecodeTree_Type)

/* -------------------------- END decodetree --------------------------- */

static PyObject *
bitarray_decode(bitarrayobject *self, PyObject *obj)
{
    binode *tree;
    PyObject *list = NULL, *symbol;
    Py_ssize_t index = 0;

    if (DecodeTree_Check(obj)) {
        tree = ((decodetreeobject *) obj)->tree;
    }
    else {
        if (check_codedict(obj) < 0)
            return NULL;

        if ((tree = binode_make_tree(obj)) == NULL)
            return NULL;
    }

    if ((list = PyList_New(0)) == NULL)
        goto error;

    while ((symbol = binode_traverse(tree, self, &index))) {
        if (PyList_Append(list, symbol) < 0)
            goto error;
    }
    if (PyErr_Occurred())
        goto error;
    if (!DecodeTree_Check(obj))
        binode_delete(tree);
    return list;

 error:
    if (!DecodeTree_Check(obj))
        binode_delete(tree);
    Py_XDECREF(list);
    return NULL;
}

PyDoc_STRVAR(decode_doc,
"decode(code, /) -> list\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays, or `decodetree`\n\
object), decode the content of the bitarray and return it as a list of\n\
symbols.");

/*********************** (bitarray) Decode Iterator ***********************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're decoding */
    binode *tree;               /* prefix tree containing symbols */
    Py_ssize_t index;           /* current index in bitarray */
    PyObject *decodetree;       /* decodetree or NULL */
} decodeiterobject;

static PyTypeObject DecodeIter_Type;

/* create a new initialized bitarray decode iterator object */
static PyObject *
bitarray_iterdecode(bitarrayobject *self, PyObject *obj)
{
    decodeiterobject *it;       /* iterator to be returned */
    binode *tree;

    if (DecodeTree_Check(obj)) {
        tree = ((decodetreeobject *) obj)->tree;
    }
    else {
        if (check_codedict(obj) < 0)
            return NULL;

        if ((tree = binode_make_tree(obj)) == NULL)
            return NULL;
    }

    it = PyObject_GC_New(decodeiterobject, &DecodeIter_Type);
    if (it == NULL) {
        if (!DecodeTree_Check(obj))
            binode_delete(tree);
        return NULL;
    }

    Py_INCREF(self);
    it->bao = self;
    it->tree = tree;
    it->index = 0;
    it->decodetree = DecodeTree_Check(obj) ? obj : NULL;
    Py_XINCREF(it->decodetree);
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(iterdecode_doc,
"iterdecode(code, /) -> iterator\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays, or `decodetree`\n\
object), decode the content of the bitarray and return an iterator over\n\
the symbols.");

static PyObject *
decodeiter_next(decodeiterobject *it)
{
    PyObject *symbol;

    symbol = binode_traverse(it->tree, it->bao, &(it->index));
    if (symbol == NULL)  /* stop iteration OR error occured */
        return NULL;
    Py_INCREF(symbol);
    return symbol;
}

static void
decodeiter_dealloc(decodeiterobject *it)
{
    if (it->decodetree)
        Py_DECREF(it->decodetree);
    else       /* when decodeiter was created from dict - free tree */
        binode_delete(it->tree);

    PyObject_GC_UnTrack(it);
    Py_DECREF(it->bao);
    PyObject_GC_Del(it);
}

static int
decodeiter_traverse(decodeiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->bao);
    return 0;
}

static PyTypeObject DecodeIter_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.decodeiterator",                /* tp_name */
    sizeof(decodeiterobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) decodeiter_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) decodeiter_traverse,       /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) decodeiter_next,           /* tp_iternext */
    0,                                        /* tp_methods */
};

/*********************** (Bitarray) Search Iterator ***********************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're searching in */
    bitarrayobject *xa;         /* bitarray being searched for */
    Py_ssize_t p;               /* current search position */
} searchiterobject;

static PyTypeObject SearchIter_Type;

/* create a new initialized bitarray search iterator object */
static PyObject *
bitarray_itersearch(bitarrayobject *self, PyObject *x)
{
    searchiterobject *it;  /* iterator to be returned */
    bitarrayobject *xa;

    if (PyIndex_Check(x)) {
        int vi;

        if ((vi = pybit_as_int(x)) < 0)
            return NULL;
        xa = (bitarrayobject *) newbitarrayobject(Py_TYPE(self), 1,
                                                  self->endian);
        if (xa == NULL)
            return NULL;
        setbit(xa, 0, vi);
    }
    else if (bitarray_Check(x)) {
        xa = (bitarrayobject *) x;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "bitarray or bool expected");
        return NULL;
    }

    if (xa->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        return NULL;
    }

    it = PyObject_GC_New(searchiterobject, &SearchIter_Type);
    if (it == NULL)
        return NULL;

    it->bao = self;
    Py_INCREF(self);
    it->xa = xa;
    if (bitarray_Check(x))
        Py_INCREF(xa);
    it->p = 0;                  /* start search at position 0 */
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(itersearch_doc,
"itersearch(sub_bitarray, /) -> iterator\n\
\n\
Searches for the given sub_bitarray in self, and return an iterator over\n\
the start positions where bitarray matches self.");

static PyObject *
searchiter_next(searchiterobject *it)
{
    Py_ssize_t p;

    p = find(it->bao, it->xa, it->p, it->bao->nbits);
    if (p < 0)  /* no more positions -- stop iteration */
        return NULL;
    it->p = p + 1;  /* next search position */
    return PyLong_FromSsize_t(p);
}

static void
searchiter_dealloc(searchiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->bao);
    Py_DECREF(it->xa);
    PyObject_GC_Del(it);
}

static int
searchiter_traverse(searchiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->bao);
    return 0;
}

static PyTypeObject SearchIter_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.searchiterator",                /* tp_name */
    sizeof(searchiterobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) searchiter_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) searchiter_traverse,       /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) searchiter_next,           /* tp_iternext */
    0,                                        /* tp_methods */
};

/*********************** bitarray method definitions **********************/

static PyMethodDef bitarray_methods[] = {
    {"all",          (PyCFunction) bitarray_all,         METH_NOARGS,
     all_doc},
    {"any",          (PyCFunction) bitarray_any,         METH_NOARGS,
     any_doc},
    {"append",       (PyCFunction) bitarray_append,      METH_O,
     append_doc},
    {"buffer_info",  (PyCFunction) bitarray_buffer_info, METH_NOARGS,
     buffer_info_doc},
    {"bytereverse",  (PyCFunction) bitarray_bytereverse, METH_NOARGS,
     bytereverse_doc},
    {"clear",        (PyCFunction) bitarray_clear,       METH_NOARGS,
     clear_doc},
    {"copy",         (PyCFunction) bitarray_copy,        METH_NOARGS,
     copy_doc},
    {"count",        (PyCFunction) bitarray_count,       METH_VARARGS,
     count_doc},
    {"decode",       (PyCFunction) bitarray_decode,      METH_O,
     decode_doc},
    {"iterdecode",   (PyCFunction) bitarray_iterdecode,  METH_O,
     iterdecode_doc},
    {"encode",       (PyCFunction) bitarray_encode,      METH_VARARGS,
     encode_doc},
    {"endian",       (PyCFunction) bitarray_endian,      METH_NOARGS,
     endian_doc},
    {"extend",       (PyCFunction) bitarray_extend,      METH_O,
     extend_doc},
    {"fill",         (PyCFunction) bitarray_fill,        METH_NOARGS,
     fill_doc},
    {"find",         (PyCFunction) bitarray_find,        METH_VARARGS,
     find_doc},
    {"frombytes",    (PyCFunction) bitarray_frombytes,   METH_O,
     frombytes_doc},
    {"fromfile",     (PyCFunction) bitarray_fromfile,    METH_VARARGS,
     fromfile_doc},
    {"index",        (PyCFunction) bitarray_index,       METH_VARARGS,
     index_doc},
    {"insert",       (PyCFunction) bitarray_insert,      METH_VARARGS,
     insert_doc},
    {"invert",       (PyCFunction) bitarray_invert,      METH_VARARGS,
     invert_doc},
    {"pack",         (PyCFunction) bitarray_pack,        METH_O,
     pack_doc},
    {"pop",          (PyCFunction) bitarray_pop,         METH_VARARGS,
     pop_doc},
    {"remove",       (PyCFunction) bitarray_remove,      METH_O,
     remove_doc},
    {"reverse",      (PyCFunction) bitarray_reverse,     METH_NOARGS,
     reverse_doc},
    {"search",       (PyCFunction) bitarray_search,      METH_VARARGS,
     search_doc},
    {"itersearch",   (PyCFunction) bitarray_itersearch,  METH_O,
     itersearch_doc},
    {"setall",       (PyCFunction) bitarray_setall,      METH_O,
     setall_doc},
    {"sort",         (PyCFunction) bitarray_sort,        METH_VARARGS |
                                                         METH_KEYWORDS,
     sort_doc},
    {"to01",         (PyCFunction) bitarray_to01,        METH_NOARGS,
     to01_doc},
    {"tobytes",      (PyCFunction) bitarray_tobytes,     METH_NOARGS,
     tobytes_doc},
    {"tofile",       (PyCFunction) bitarray_tofile,      METH_O,
     tofile_doc},
    {"tolist",       (PyCFunction) bitarray_tolist,      METH_NOARGS,
     tolist_doc},
    {"unpack",       (PyCFunction) bitarray_unpack,      METH_VARARGS |
                                                         METH_KEYWORDS,
     unpack_doc},

    {"__copy__",     (PyCFunction) bitarray_copy,        METH_NOARGS,
     copy_doc},
    {"__deepcopy__", (PyCFunction) bitarray_copy,        METH_O,
     copy_doc},
    {"__reduce__",   (PyCFunction) bitarray_reduce,      METH_NOARGS,
     reduce_doc},
    {"__sizeof__",   (PyCFunction) bitarray_sizeof,      METH_NOARGS,
     sizeof_doc},

    {NULL,           NULL}  /* sentinel */
};

/* ------------------------ bitarray initialization -------------------- */

static PyObject *
bitarray_from_index(PyTypeObject *type, PyObject *index, int endian)
{
    Py_ssize_t nbits;

    assert(PyIndex_Check(index));
    nbits = PyNumber_AsSsize_t(index, PyExc_OverflowError);
    if (nbits == -1 && PyErr_Occurred())
        return NULL;

    if (nbits < 0) {
        PyErr_SetString(PyExc_ValueError, "bitarray length must be >= 0");
        return NULL;
    }
    return newbitarrayobject(type, nbits, endian);
}

/* The head byte % 8 specifies the number of unused bits (in last buffer
   byte), the remaining bytes consist of the buffer itself */
static PyObject *
unpickle(PyTypeObject *type, PyObject *bytes, int endian)
{
    PyObject *res;
    Py_ssize_t nbytes;
    unsigned char head;
    char *data;

    assert(PyBytes_Check(bytes));
    nbytes = PyBytes_GET_SIZE(bytes);
    assert(nbytes > 0);
    data = PyBytes_AS_STRING(bytes);
    head = *data;

    if (nbytes == 1 && head % 8)
        return PyErr_Format(PyExc_ValueError,
                            "invalid header byte 0x%02x", head);

    res = newbitarrayobject(type,
                            BITS(nbytes - 1) - ((Py_ssize_t) (head % 8)),
                            endian);
    if (res == NULL)
        return NULL;
    memcpy(((bitarrayobject *) res)->ob_item, data + 1, (size_t) nbytes - 1);
    return res;
}

static PyObject *
bitarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *res;  /* to be returned in some cases */
    PyObject *initial = NULL;
    char *endian_str = NULL;
    int endian;
    static char *kwlist[] = {"", "endian", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Os:bitarray",
                                     kwlist, &initial, &endian_str))
        return NULL;

    endian = endian_from_string(endian_str);
    if (endian < 0)
        return NULL;

    /* no arg or None */
    if (initial == NULL || initial == Py_None)
        return newbitarrayobject(type, 0, endian);

    /* bool */
    if (PyBool_Check(initial)) {
        PyErr_SetString(PyExc_TypeError, "cannot create bitarray from bool");
        return NULL;
    }

    /* index (a number) */
    if (PyIndex_Check(initial))
        return bitarray_from_index(type, initial, endian);

    /* bytes (for pickling) */
    if (PyBytes_Check(initial) && PyBytes_GET_SIZE(initial) > 0) {
        unsigned char head = *PyBytes_AS_STRING(initial);

        if (head < 32 && head % 16 < 8) {
            if (endian_str == NULL)  /* no endianness provided */
                endian = head / 16 ? ENDIAN_BIG : ENDIAN_LITTLE;
            return unpickle(type, initial, endian);
        }
    }

    if (bitarray_Check(initial) && endian_str == NULL)
        endian = ((bitarrayobject *) initial)->endian;

    /* leave remaining type dispatch to extend method */
    res = newbitarrayobject(type, 0, endian);
    if (res == NULL)
        return NULL;
    if (extend_dispatch((bitarrayobject *) res, initial) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

static PyObject *
richcompare(PyObject *v, PyObject *w, int op)
{
    int cmp;
    Py_ssize_t i, vs, ws;

    if (!bitarray_Check(v) || !bitarray_Check(w)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
#define va  ((bitarrayobject *) v)
#define wa  ((bitarrayobject *) w)
    vs = va->nbits;
    ws = wa->nbits;
    if (op == Py_EQ || op == Py_NE) {
        /* shortcuts for EQ/NE */
        if (vs != ws) {
            /* if sizes differ, the bitarrays differ */
            return PyBool_FromLong(op == Py_NE);
        }
        else if (va->endian == wa->endian) {
            /* sizes and endianness are the same - use memcmp() */
            assert(vs == ws && Py_SIZE(v) == Py_SIZE(w));
            setunused(va);
            setunused(wa);
            cmp = memcmp(va->ob_item, wa->ob_item, (size_t) Py_SIZE(v));
            return PyBool_FromLong((cmp == 0) ^ (op == Py_NE));
        }
    }

    /* search for the first index where items are different */
    for (i = 0; i < vs && i < ws; i++) {
        int vi = GETBIT(va, i);
        int wi = GETBIT(wa, i);

        if (vi != wi) {
            /* we have an item that differs */
            switch (op) {
            case Py_LT: cmp = vi <  wi; break;
            case Py_LE: cmp = vi <= wi; break;
            case Py_EQ: cmp = 0; break;
            case Py_NE: cmp = 1; break;
            case Py_GT: cmp = vi >  wi; break;
            case Py_GE: cmp = vi >= wi; break;
            default: return NULL;  /* cannot happen */
            }
            return PyBool_FromLong((long) cmp);
        }
    }
#undef va
#undef wa

    /* no more items to compare -- compare sizes */
    switch (op) {
    case Py_LT: cmp = vs <  ws; break;
    case Py_LE: cmp = vs <= ws; break;
    case Py_EQ: cmp = vs == ws; break;
    case Py_NE: cmp = vs != ws; break;
    case Py_GT: cmp = vs >  ws; break;
    case Py_GE: cmp = vs >= ws; break;
    default: return NULL;  /* cannot happen */
    }
    return PyBool_FromLong((long) cmp);
}

/***************************** bitarray iterator **************************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're iterating over */
    Py_ssize_t index;                /* current index in bitarray */
} bitarrayiterobject;

static PyTypeObject BitarrayIter_Type;

/* create a new initialized bitarray iterator object, this object is
   returned when calling iter(a) */
static PyObject *
bitarray_iter(bitarrayobject *self)
{
    bitarrayiterobject *it;

    it = PyObject_GC_New(bitarrayiterobject, &BitarrayIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->bao = self;
    it->index = 0;
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

static PyObject *
bitarrayiter_next(bitarrayiterobject *it)
{
    long vi;

    if (it->index < it->bao->nbits) {
        vi = GETBIT(it->bao, it->index);
        it->index++;
        return PyLong_FromLong(vi);
    }
    return NULL;  /* stop iteration */
}

static void
bitarrayiter_dealloc(bitarrayiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->bao);
    PyObject_GC_Del(it);
}

static int
bitarrayiter_traverse(bitarrayiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->bao);
    return 0;
}

static PyTypeObject BitarrayIter_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.bitarrayiterator",              /* tp_name */
    sizeof(bitarrayiterobject),               /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) bitarrayiter_dealloc,        /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) bitarrayiter_traverse,     /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) bitarrayiter_next,         /* tp_iternext */
    0,                                        /* tp_methods */
};

/*********************** bitarray buffer interface ************************/

#if PY_MAJOR_VERSION == 2       /* old buffer protocol */
static Py_ssize_t
bitarray_buffer_getreadbuf(bitarrayobject *self,
                           Py_ssize_t index, const void **ptr)
{
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError, "accessing non-existent segment");
        return -1;
    }
    *ptr = (void *) self->ob_item;
    return Py_SIZE(self);
}

static Py_ssize_t
bitarray_buffer_getwritebuf(bitarrayobject *self,
                            Py_ssize_t index, const void **ptr)
{
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError, "accessing non-existent segment");
        return -1;
    }
    *ptr = (void *) self->ob_item;
    return Py_SIZE(self);
}

static Py_ssize_t
bitarray_buffer_getsegcount(bitarrayobject *self, Py_ssize_t *lenp)
{
    if (lenp)
        *lenp = Py_SIZE(self);
    return 1;
}

static Py_ssize_t
bitarray_buffer_getcharbuf(bitarrayobject *self,
                           Py_ssize_t index, const char **ptr)
{
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError, "accessing non-existent segment");
        return -1;
    }
    *ptr = self->ob_item;
    return Py_SIZE(self);
}

#endif

static int
bitarray_getbuffer(bitarrayobject *self, Py_buffer *view, int flags)
{
    int ret;

    if (view == NULL) {
        self->ob_exports++;
        return 0;
    }
    ret = PyBuffer_FillInfo(view, (PyObject *) self,
                            (void *) self->ob_item,
                            Py_SIZE(self), 0, flags);
    if (ret >= 0)
        self->ob_exports++;

    return ret;
}

static void
bitarray_releasebuffer(bitarrayobject *self, Py_buffer *view)
{
    self->ob_exports--;
}

static PyBufferProcs bitarray_as_buffer = {
#if PY_MAJOR_VERSION == 2       /* old buffer protocol */
    (readbufferproc) bitarray_buffer_getreadbuf,
    (writebufferproc) bitarray_buffer_getwritebuf,
    (segcountproc) bitarray_buffer_getsegcount,
    (charbufferproc) bitarray_buffer_getcharbuf,
#endif
    (getbufferproc) bitarray_getbuffer,
    (releasebufferproc) bitarray_releasebuffer,
};

/***************************** Bitarray Type ******************************/

PyDoc_STRVAR(bitarraytype_doc,
"bitarray(initializer=0, /, endian='big') -> bitarray\n\
\n\
Return a new bitarray object whose items are bits initialized from\n\
the optional initial object, and endianness.\n\
The initializer may be of the following types:\n\
\n\
`int`: Create a bitarray of given integer length.  The initial values are\n\
uninitialized.\n\
\n\
`str`: Create bitarray from a string of `0` and `1`.\n\
\n\
`iterable`: Create bitarray from iterable or sequence or integers 0 or 1.\n\
\n\
The optional keyword arguments `endian` specifies the bit endianness of the\n\
created bitarray object.\n\
Allowed values are the strings `big` and `little` (default is `big`).\n\
The bit endianness only effects the when buffer representation of the\n\
bitarray.");

static PyTypeObject Bitarray_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.bitarray",                      /* tp_name */
    sizeof(bitarrayobject),                   /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) bitarray_dealloc,            /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    (reprfunc) bitarray_repr,                 /* tp_repr */
    &bitarray_as_number,                      /* tp_as_number*/
    &bitarray_as_sequence,                    /* tp_as_sequence */
    &bitarray_as_mapping,                     /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    &bitarray_as_buffer,                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_WEAKREFS
#if PY_MAJOR_VERSION == 2
    | Py_TPFLAGS_HAVE_NEWBUFFER | Py_TPFLAGS_CHECKTYPES
#endif
    ,                                         /* tp_flags */
    bitarraytype_doc,                         /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    richcompare,                              /* tp_richcompare */
    offsetof(bitarrayobject, weakreflist),    /* tp_weaklistoffset */
    (getiterfunc) bitarray_iter,              /* tp_iter */
    0,                                        /* tp_iternext */
    bitarray_methods,                         /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    PyType_GenericAlloc,                      /* tp_alloc */
    bitarray_new,                             /* tp_new */
    PyObject_Del,                             /* tp_free */
};

/***************************** Module functions ***************************/

static PyObject *
get_default_endian(PyObject *module)
{
    return Py_BuildValue("s",
                         default_endian == ENDIAN_LITTLE ? "little" : "big");
}

PyDoc_STRVAR(get_default_endian_doc,
"get_default_endian() -> string\n\
\n\
Return the default endianness for new bitarray objects being created.\n\
Unless `_set_default_endian()` is called, the return value is `big`.");


static PyObject *
set_default_endian(PyObject *module, PyObject *args)
{
    char *endian_str;
    int tmp;

    if (!PyArg_ParseTuple(args, "s:_set_default_endian", &endian_str))
        return NULL;

    /* As endian_from_string() might return -1, we have to store its value
       in a temporary variable BEFORE setting default_endian. */
    tmp = endian_from_string(endian_str);
    if (tmp < 0)
        return NULL;
    default_endian = tmp;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(set_default_endian_doc,
"_set_default_endian(endian, /)\n\
\n\
Set the default bit endianness for new bitarray objects being created.");


static PyObject *
sysinfo(void)
{
    return Py_BuildValue("iiiin",
                         (int) sizeof(void *),
                         (int) sizeof(size_t),
                         (int) sizeof(Py_ssize_t),
                         (int) sizeof(Py_ssize_t),
                         PY_SSIZE_T_MAX);
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo() -> tuple\n\
\n\
tuple(sizeof(void *),\n\
      sizeof(size_t),\n\
      sizeof(Py_ssize_t),\n\
      sizeof(Py_ssize_t),\n\
      PY_SSIZE_T_MAX)");


static PyMethodDef module_functions[] = {
    {"get_default_endian",  (PyCFunction) get_default_endian,
                                      METH_NOARGS,  get_default_endian_doc},
    {"_set_default_endian", (PyCFunction) set_default_endian,
                                      METH_VARARGS, set_default_endian_doc},
    {"_sysinfo",            (PyCFunction) sysinfo,
                                      METH_NOARGS,  sysinfo_doc           },
    {NULL,         NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

#ifdef IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_bitarray", 0, -1, module_functions,
};
#endif

PyMODINIT_FUNC
#ifdef IS_PY3K
PyInit__bitarray(void)
#else
init_bitarray(void)
#endif
{
    PyObject *m;

#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3("_bitarray", module_functions, 0);
#endif
    if (m == NULL)
        goto error;

    if (PyType_Ready(&Bitarray_Type) < 0)
        goto error;
    Py_SET_TYPE(&Bitarray_Type, &PyType_Type);
    Py_INCREF((PyObject *) &Bitarray_Type);
    PyModule_AddObject(m, "bitarray", (PyObject *) &Bitarray_Type);

    if (PyType_Ready(&DecodeTree_Type) < 0)
        goto error;
    Py_SET_TYPE(&DecodeTree_Type, &PyType_Type);
    Py_INCREF((PyObject *) &DecodeTree_Type);
    PyModule_AddObject(m, "decodetree", (PyObject *) &DecodeTree_Type);

    if (PyType_Ready(&DecodeIter_Type) < 0)
        goto error;
    Py_SET_TYPE(&DecodeIter_Type, &PyType_Type);

    if (PyType_Ready(&BitarrayIter_Type) < 0)
        goto error;
    Py_SET_TYPE(&BitarrayIter_Type, &PyType_Type);

    if (PyType_Ready(&SearchIter_Type) < 0)
        goto error;
    Py_SET_TYPE(&SearchIter_Type, &PyType_Type);

    PyModule_AddObject(m, "__version__",
                       Py_BuildValue("s", BITARRAY_VERSION));
#ifdef IS_PY3K
    return m;
 error:
    return NULL;
#else
 error:
    return;
#endif
}
