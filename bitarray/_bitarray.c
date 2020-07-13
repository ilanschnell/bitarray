/*
   Copyright (c) 2008 - 2020, Ilan Schnell
   bitarray is published under the PSF license.

   This file is the C part of the bitarray package.
   All functionality is implemented here.

   Author: Ilan Schnell
*/
#define BITARRAY_VERSION  "1.4.1"

#define PY_SSIZE_T_CLEAN
#include "Python.h"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

#ifdef IS_PY3K
#define Py_TPFLAGS_HAVE_WEAKREFS  0
#else
/* this macro was introduced in Python 3.3 */
#define Py_MIN(x, y)  (((x) > (y)) ? (y) : (x))
#endif

#if PY_MAJOR_VERSION == 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 7)
/* (new) buffer protocol */
#define WITH_BUFFER
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#else  /* !STDC_HEADERS */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      /* For size_t */
#endif /* HAVE_SYS_TYPES_H */
#endif /* !STDC_HEADERS */


/* instead of Py_ssize_t, we use this type indices, as Py_ssize_t is
   only 4 bytes on 32bit machines, but bitarray indices can exceed this */
typedef long long int idx_t;

/* Unlike the normal convention, ob_size is the byte count, not the number
   of elements.  The reason for doing this is that we can use our own
   special idx_t for the number of bits, which may exceed 2^32 on a 32 bit
   machine.  */
typedef struct {
    PyObject_VAR_HEAD
    char *ob_item;
    Py_ssize_t allocated;       /* how many bytes allocated */
    idx_t nbits;                /* length of bitarray, i.e. elements */
    int endian;                 /* bit endianness of bitarray */
    int ob_exports;             /* how many buffer exports */
    PyObject *weakreflist;      /* list of weak references */
} bitarrayobject;

static PyTypeObject Bitarraytype;

/* --- bit endianness --- */
#define ENDIAN_LITTLE  0
#define ENDIAN_BIG     1
#define ENDIAN_INT(i)  ((i) == ENDIAN_LITTLE ? "little" : "big")
#define ENDIAN_OBJ(o)  ENDIAN_INT(((bitarrayobject *) o)->endian)
static int default_endian = ENDIAN_BIG;

#define bitarray_Check(obj)  PyObject_TypeCheck((obj), &Bitarraytype)

#define BITS(bytes)  ((idx_t) (bytes) << 3)

/* number of bytes necessary to store given bits */
#define BYTES(bits)  (((bits) == 0) ? 0 : (((bits) - 1) / 8 + 1))

#define BITMASK(endian, i)  \
    (((char) 1) << ((endian) == ENDIAN_LITTLE ? ((i) % 8) : (7 - (i) % 8)))

/* This (bytes) block size is used when reading/writing blocks of bytes
   from files. */
#define BLOCKSIZE  65536

/* ------------ low level access to bits in bitarrayobject ------------- */

#ifndef NDEBUG
static int GETBIT(bitarrayobject *self, idx_t i) {
    assert(0 <= i && i < self->nbits);
    return ((self)->ob_item[(i) / 8] & BITMASK((self)->endian, i) ? 1 : 0);
}
#else
#define GETBIT(self, i)  \
    ((self)->ob_item[(i) / 8] & BITMASK((self)->endian, i) ? 1 : 0)
#endif

static void
setbit(bitarrayobject *self, idx_t i, int bit)
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

static int
check_overflow(idx_t nbits)
{
    assert(nbits >= 0);
    if (sizeof(void *) == 4) {  /* 32bit system */
        const idx_t max_bits = ((idx_t) 1) << 34;  /* 2^34 = 16 Gbits*/
        if (nbits > max_bits) {
            PyErr_Format(PyExc_OverflowError,
                         "cannot create bitarray of size %lld, "
                         "max size is %lld", nbits, max_bits);
            return -1;
        }
    }
    return 0;
}

static int
resize(bitarrayobject *self, idx_t nbits)
{
    const Py_ssize_t allocated = self->allocated, size = Py_SIZE(self);
    Py_ssize_t newsize;
    size_t new_allocated;

    assert(allocated >= size && size == BYTES(self->nbits));
    /* ob_item == NULL implies ob_size == allocated == 0 */
    assert(self->ob_item != NULL || (size == 0 && allocated == 0));
    /* allocated ==0 implies size == 0 */
    assert(allocated != 0 || size == 0);

    if (check_overflow(nbits) < 0)
        return -1;
    newsize = (Py_ssize_t) BYTES(nbits);

    if (newsize == size) {
        /* the memory size hasn't changed - bypass almost everything */
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
        Py_SIZE(self) = newsize;
        self->nbits = nbits;
        return 0;
    }

    if (newsize == 0) {
        PyMem_FREE(self->ob_item);
        self->ob_item = NULL;
        Py_SIZE(self) = 0;
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
    Py_SIZE(self) = newsize;
    self->allocated = new_allocated;
    self->nbits = nbits;
    return 0;
}

/* create new bitarray object without initialization of buffer */
static PyObject *
newbitarrayobject(PyTypeObject *type, idx_t nbits, int endian)
{
    bitarrayobject *obj;
    Py_ssize_t nbytes;

    if (check_overflow(nbits) < 0)
        return NULL;

    obj = (bitarrayobject *) type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    nbytes = (Py_ssize_t) BYTES(nbits);
    Py_SIZE(obj) = nbytes;
    if (nbytes == 0) {
        obj->ob_item = NULL;
    }
    else {
        obj->ob_item = (char *) PyMem_Malloc((size_t) nbytes);
        if (obj->ob_item == NULL) {
            PyObject_Del(obj);
            PyErr_NoMemory();
            return NULL;
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

/* copy n bits from other (starting at b) onto self (starting at a) */
static void
copy_n(bitarrayobject *self, idx_t a,
       bitarrayobject *other, idx_t b, idx_t n)
{
    idx_t i;

    assert(0 <= n && n <= self->nbits && n <= other->nbits);
    assert(0 <= a && a <= self->nbits - n);
    assert(0 <= b && b <= other->nbits - n);
    if (n == 0)
        return;

    /* When the start positions are at byte positions, we can copy whole
       bytes using memmove, and copy the remaining few bits individually.
       Note that the order of these two operations matters when copying
       self to self. */
    if (self->endian == other->endian && a % 8 == 0 && b % 8 == 0 && n >= 8)
    {
        const Py_ssize_t bytes = (Py_ssize_t) n / 8;
        const idx_t bits = BITS(bytes);

        assert(bits <= n && n < bits + 8);
        if (a <= b)
            memmove(self->ob_item + a / 8, other->ob_item + b / 8, bytes);

        if (n != bits)
            copy_n(self, bits + a, other, bits + b, n - bits);

        if (a > b)
            memmove(self->ob_item + a / 8, other->ob_item + b / 8, bytes);

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
delete_n(bitarrayobject *self, idx_t start, idx_t n)
{
    assert(0 <= start && start <= self->nbits);
    assert(0 <= n && n <= self->nbits - start);
    if (n == 0)
        return 0;

    copy_n(self, start, self, start + n, self->nbits - start - n);
    return resize(self, self->nbits - n);
}

/* starting at start, insert n (uninitialized) bits into self */
static int
insert_n(bitarrayobject *self, idx_t start, idx_t n)
{
    assert(0 <= start && start <= self->nbits);
    assert(n >= 0);
    if (n == 0)
        return 0;

    if (resize(self, self->nbits + n) < 0)
        return -1;
    copy_n(self, start + n, self, start, self->nbits - start - n);
    return 0;
}

/* sets ususet bits to 0, i.e. the ones in the last byte (if any),
   and return the number of bits set -- self->nbits is unchanged */
static int
setunused(bitarrayobject *self)
{
    const idx_t n = BITS(Py_SIZE(self));
    idx_t i;

    for (i = self->nbits; i < n; i++)
        setbit(self, i, 0);
    assert(n - self->nbits < 8);
    return (int) (n - self->nbits);
}

/* repeat self n times */
static int
repeat(bitarrayobject *self, idx_t n)
{
    idx_t nbits, i;

    if (n <= 0) {
        if (resize(self, 0) < 0)
            return -1;
    }
    if (n > 1) {
        nbits = self->nbits;
        if (resize(self, nbits * n) < 0)
            return -1;
        for (i = 1; i < n; i++)
            copy_n(self, i * nbits, self, 0, nbits);
    }
    return 0;
}


enum op_type {
    OP_and,
    OP_or,
    OP_xor,
};

/* perform bitwise in-place operation */
static int
bitwise(bitarrayobject *self, PyObject *arg, enum op_type oper)
{
    bitarrayobject *other;
    Py_ssize_t n = Py_SIZE(self), i;

    if (!bitarray_Check(arg)) {
        PyErr_SetString(PyExc_TypeError,
                        "bitarray expected for bitwise operation");
        return -1;
    }
    other = (bitarrayobject *) arg;
    if (self->nbits != other->nbits) {
        PyErr_SetString(PyExc_ValueError,
               "bitarrays of equal length expected for bitwise operation");
        return -1;
    }
    setunused(self);
    setunused(other);
    switch (oper) {
    case OP_and:
        for (i = 0; i < n; i++)
            self->ob_item[i] &= other->ob_item[i];
        break;
    case OP_or:
        for (i = 0; i < n; i++)
            self->ob_item[i] |= other->ob_item[i];
        break;
    case OP_xor:
        for (i = 0; i < n; i++)
            self->ob_item[i] ^= other->ob_item[i];
        break;
    default:  /* should never happen */
        return -1;
    }
    return 0;
}

/* set the bits from start to stop (excluding) in self to val */
static void
setrange(bitarrayobject *self, idx_t start, idx_t stop, int val)
{
    idx_t i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (self->nbits == 0 || start >= stop)
        return;

    if (stop >= start + 8) {
        const Py_ssize_t byte_start = BYTES(start);
        const Py_ssize_t byte_stop = (Py_ssize_t) stop / 8;
        for (i = start; i < BITS(byte_start); i++)
            setbit(self, i, val);
        memset(self->ob_item + byte_start, val ? 0xff : 0x00,
               byte_stop - byte_start);
        for (i = BITS(byte_stop); i < stop; i++)
            setbit(self, i, val);
    }
    else {
        for (i = start; i < stop; i++)
            setbit(self, i, val);
    }
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

/* Return number of 1 bits.  This function never fails. */
static idx_t
count(bitarrayobject *self, int vi, idx_t start, idx_t stop)
{
    idx_t i, res = 0;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);
    assert(0 <= vi && vi <= 1);
    assert(BYTES(stop) <= Py_SIZE(self));

    if (self->nbits == 0 || start >= stop)
        return 0;

    if (stop >= start + 8) {
        const Py_ssize_t byte_start = BYTES(start);
        const Py_ssize_t byte_stop = (Py_ssize_t) (stop / 8);
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

/* Return index of first occurrence of vi, -1 when x is not in found.
   This function never fails. */
static idx_t
findfirst(bitarrayobject *self, int vi, idx_t start, idx_t stop)
{
    Py_ssize_t j;
    idx_t i;

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
        for (j = (Py_ssize_t) (start / 8); j < BYTES(stop); j++)
            if (c ^ self->ob_item[j])
                break;

        if (start < BITS(j))
            start = BITS(j);
    }

    /* fine grained search */
    for (i = start; i < stop; i++)
        if (GETBIT(self, i) == vi)
            return i;

    return -1;
}

/* search for the first occurrence of bitarray xa (in self), starting at p,
   and return its position (or -1 when not found)
*/
static idx_t
search(bitarrayobject *self, bitarrayobject *xa, idx_t p)
{
    idx_t i;

    assert(p >= 0);
    while (p < self->nbits - xa->nbits + 1) {
        for (i = 0; i < xa->nbits; i++)
            if (GETBIT(self, p + i) != GETBIT(xa, i))
                goto next;

        return p;
    next:
        p++;
    }
    return -1;
}

static int
set_item(bitarrayobject *self, idx_t i, PyObject *v)
{
    int vi;

    assert(0 <= i && i < self->nbits);
    vi = PyObject_IsTrue(v);
    if (vi < 0)
        return -1;
    setbit(self, i, vi);
    return 0;
}

static int
append_item(bitarrayobject *self, PyObject *item)
{
    if (resize(self, self->nbits + 1) < 0)
        return -1;
    return set_item(self, self->nbits - 1, item);
}

static int
extend_bitarray(bitarrayobject *self, bitarrayobject *other)
{
    /* We have to store the sizes before we resize, and since
       other may be self, we also need to store other->nbits. */
    idx_t self_nbits = self->nbits;
    idx_t other_nbits = other->nbits;

    if (other_nbits == 0)
        return 0;

    if (resize(self, self_nbits + other_nbits) < 0)
        return -1;

    copy_n(self, self_nbits, other, 0, other_nbits);
    return 0;
}

static int
extend_iter(bitarrayobject *self, PyObject *iter)
{
    PyObject *item;

    assert(PyIter_Check(iter));
    while ((item = PyIter_Next(iter)) != NULL) {
        if (append_item(self, item) < 0) {
            Py_DECREF(item);
            return -1;
        }
        Py_DECREF(item);
    }
    if (PyErr_Occurred())
        return -1;

    return 0;
}

static int
extend_list(bitarrayobject *self, PyObject *list)
{
    PyObject *item;
    Py_ssize_t n, i;

    assert(PyList_Check(list));
    n = PyList_Size(list);
    if (n == 0)
        return 0;

    if (resize(self, self->nbits + n) < 0)
        return -1;

    for (i = 0; i < n; i++) {
        item = PyList_GetItem(list, i);
        if (item == NULL)
            return -1;
        if (set_item(self, self->nbits - n + i, item) < 0)
            return -1;
    }
    return 0;
}

static int
extend_tuple(bitarrayobject *self, PyObject *tuple)
{
    PyObject *item;
    Py_ssize_t n, i;

    assert(PyTuple_Check(tuple));
    n = PyTuple_Size(tuple);
    if (n == 0)
        return 0;

    if (resize(self, self->nbits + n) < 0)
        return -1;

    for (i = 0; i < n; i++) {
        item = PyTuple_GetItem(tuple, i);
        if (item == NULL)
            return -1;
        if (set_item(self, self->nbits - n + i, item) < 0)
            return -1;
    }
    return 0;
}

/* extend_bytes(): extend the bitarray from a PyBytes object, where each
   whole character is converted to a single bit */
enum conv_t {
    BYTES_01,    /*  '0' -> 0    '1'  -> 1   no other characters allowed */
    BYTES_RAW,   /*  0x00 -> 0   other -> 1                              */
};

static int
extend_bytes(bitarrayobject *self, PyObject *bytes, enum conv_t conv)
{
    Py_ssize_t nbytes, i;
    char c, *data;
    int vi = 0;

    assert(PyBytes_Check(bytes));
    nbytes = PyBytes_Size(bytes);
    if (nbytes == 0)
        return 0;

    if (resize(self, self->nbits + nbytes) < 0)
        return -1;

    data = PyBytes_AsString(bytes);

    for (i = 0; i < nbytes; i++) {
        c = data[i];
        /* depending on conv, map c to bit */
        switch (conv) {
        case BYTES_01:
            switch (c) {
            case '0': vi = 0; break;
            case '1': vi = 1; break;
            default:
                PyErr_Format(PyExc_ValueError,
                             "character must be '0' or '1', found '%c'", c);
                return -1;
            }
            break;
        case BYTES_RAW:
            vi = c ? 1 : 0;
            break;
        default:  /* should never happen */
            return -1;
        }
        setbit(self, self->nbits - nbytes + i, vi);
    }
    return 0;
}

static int
extend_dispatch(bitarrayobject *self, PyObject *obj)
{
    PyObject *iter;
    int ret;

    /* dispatch on type */
    if (bitarray_Check(obj))                              /* bitarray */
        return extend_bitarray(self, (bitarrayobject *) obj);

    if (PyList_Check(obj))                                    /* list */
        return extend_list(self, obj);

    if (PyTuple_Check(obj))                                  /* tuple */
        return extend_tuple(self, obj);

    if (PyBytes_Check(obj))                               /* bytes 01 */
        return extend_bytes(self, obj, BYTES_01);

    if (PyUnicode_Check(obj)) {                /* (unicode) string 01 */
        PyObject *bytes;
        bytes = PyUnicode_AsEncodedString(obj, NULL, NULL);
        ret = extend_bytes(self, bytes, BYTES_01);
        Py_DECREF(bytes);
        return ret;
    }

    if (PyIter_Check(obj))                                    /* iter */
        return extend_iter(self, obj);

    /* finally, try to get the iterator of the object */
    iter = PyObject_GetIter(obj);
    if (iter) {
        ret = extend_iter(self, iter);
        Py_DECREF(iter);
        return ret;
    }
    PyErr_Format(PyExc_TypeError,
                 "'%s' object is not iterable", Py_TYPE(obj)->tp_name);
    return -1;
}

/* This function returns either a PyString or PyBytes object depending
   on the parameter 'out' (and the Python version).  The function is meant
   to be called in bitarray_unpack(), bitarray_to01() and bitarray_repr().

    name              used in            returns Py3    returns Py2
    ---------------------------------------------------------------- */
enum output_t {
    OUT_unpack,  /*   bitarray_unpack()  PyBytes        PyString     */
    OUT_to01,    /*   bitarray_to01()    PyString       PyString     */
    OUT_repr,    /*   bitarray_repr()    PyString       PyString     */
};

static PyObject *
unpack(bitarrayobject *self, char zero, char one, enum output_t out)
{
    PyObject *result;
    Py_ssize_t i;
    char *str;
    size_t strsize;
    int offset = 0;     /* this is used when copying bytes into str */

    if (self->nbits > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "bitarray too large to unpack");
        return NULL;
    }

    /* note that 12 is the length of "bitarray('')" */
    strsize = self->nbits + (out == OUT_repr ? 12 : 0);
    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    if (out == OUT_repr) {
        /* add "bitarray('......')" to str */
        strcpy(str, "bitarray('"); /* has length 10 */
        str[strsize - 2] = '\'';
        str[strsize - 1] = ')';
        offset = 10;
    }
    for (i = 0; i < self->nbits; i++)
        str[i + offset] = GETBIT(self, i) ? one : zero;

    result = Py_BuildValue(
               (out == OUT_unpack && PY_MAJOR_VERSION == 3) ? "y#" : "s#",
               str, (Py_ssize_t) strsize);
    PyMem_Free((void *) str);
    return result;
}

/* --------- helper functions not involving bitarrayobjects ------------ */

#ifdef IS_PY3K
#define IS_INDEX(x)  (PyLong_Check(x) || PyIndex_Check(x))
#define IS_INT_OR_BOOL(x)  (PyBool_Check(x) || PyLong_Check(x))
#else  /* Py 2 */
#define IS_INDEX(x)  (PyInt_Check(x) || PyLong_Check(x) || PyIndex_Check(x))
#define IS_INT_OR_BOOL(x)  (PyBool_Check(x) || PyInt_Check(x) || \
                                               PyLong_Check(x))
#endif

/* given an PyLong (which must be 0 or 1), or a PyBool, return 0 or 1,
   or -1 on error */
static int
IntBool_AsInt(PyObject *v)
{
    long x;

    if (PyBool_Check(v))
        return PyObject_IsTrue(v);

#ifndef IS_PY3K
    if (PyInt_Check(v)) {
        x = PyInt_AsLong(v);
    }
    else
#endif
    if (PyLong_Check(v)) {
        x = PyLong_AsLong(v);
    }
    else {
        PyErr_SetString(PyExc_TypeError, "integer or bool expected");
        return -1;
    }

    if (x < 0 || x > 1) {
        PyErr_SetString(PyExc_ValueError,
                        "integer value between 0 and 1 expected");
        return -1;
    }
    return (int) x;
}

/* Normalize index (which may be negative), such that 0 <= i <= n */
static void
normalize_index(idx_t n, idx_t *i)
{
    if (*i < 0) {
        *i += n;
        if (*i < 0)
            *i = 0;
    }
    if (*i > n)
        *i = n;
}

/* Extract a slice index from a PyInt or PyLong or an object with the
   nb_index slot defined, and store in *i.
   However, this function returns -1 on error and 0 on success.

   This is almost _PyEval_SliceIndex() with Py_ssize_t replaced by idx_t
*/
static int
getIndex(PyObject *v, idx_t *i)
{
    idx_t x;

#ifndef IS_PY3K
    if (PyInt_Check(v)) {
        x = PyInt_AS_LONG(v);
    }
    else
#endif
    if (PyLong_Check(v)) {
        x = PyLong_AsLongLong(v);
    }
    else if (PyIndex_Check(v)) {
        x = PyNumber_AsSsize_t(v, NULL);
        if (x == -1 && PyErr_Occurred())
            return -1;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "slice indices must be integers or "
                                         "None or have an __index__ method");
        return -1;
    }
    *i = x;
    return 0;
}

/* this is PySlice_GetIndicesEx() with Py_ssize_t replaced by idx_t */
static int
slice_GetIndicesEx(PySliceObject *r, idx_t length,
                   idx_t *start, idx_t *stop, idx_t *step, idx_t *slicelength)
{
    idx_t defstart, defstop;

    if (r->step == Py_None) {
        *step = 1;
    }
    else {
        if (getIndex(r->step, step) < 0)
            return -1;
        if (*step == 0) {
            PyErr_SetString(PyExc_ValueError, "slice step cannot be zero");
            return -1;
        }
    }
    defstart = *step < 0 ? length - 1 : 0;
    defstop = *step < 0 ? -1 : length;

    if (r->start == Py_None) {
        *start = defstart;
    }
    else {
        if (getIndex(r->start, start) < 0)
            return -1;
        if (*start < 0) *start += length;
        if (*start < 0) *start = (*step < 0) ? -1 : 0;
        if (*start >= length) *start = (*step < 0) ? length - 1 : length;
    }

    if (r->stop == Py_None) {
        *stop = defstop;
    }
    else {
        if (getIndex(r->stop, stop) < 0)
            return -1;
        if (*stop < 0) *stop += length;
        if (*stop < 0) *stop = -1;
        if (*stop > length) *stop = length;
    }

    if ((*step < 0 && *stop >= *start) || (*step > 0 && *start >= *stop)) {
        *slicelength = 0;
    }
    else if (*step < 0) {
        *slicelength = (*stop - *start + 1) / (*step) + 1;
    }
    else {
        *slicelength = (*stop - *start - 1) / (*step) + 1;
    }

    return 0;
}

/**************************************************************************
                     Implementation of bitarray methods
 **************************************************************************/

static PyObject *
bitarray_length(bitarrayobject *self)
{
    return PyLong_FromLongLong(self->nbits);
}

PyDoc_STRVAR(length_doc,
"length() -> int\n\
\n\
Return the length, i.e. number of bits stored in the bitarray.\n\
This method is preferred over `__len__` (used when typing `len(a)`),\n\
since `__len__` will fail for a bitarray object with 2^31 or more elements\n\
on a 32bit machine, whereas this method will return the correct value,\n\
on 32bit and 64bit machines.");

PyDoc_STRVAR(len_doc,
"__len__() -> int\n\
\n\
Return the length, i.e. number of bits stored in the bitarray.\n\
This method will fail for a bitarray object with 2^31 or more elements\n\
on a 32bit machine.  Use bitarray.length() instead.");


static PyObject *
bitarray_copy(bitarrayobject *self)
{
    PyObject *res;

    res = newbitarrayobject(Py_TYPE(self), self->nbits, self->endian);
    if (res == NULL)
        return NULL;

    memcpy(((bitarrayobject *) res)->ob_item, self->ob_item, Py_SIZE(self));
    return res;
}

PyDoc_STRVAR(copy_doc,
"copy() -> bitarray\n\
\n\
Return a copy of the bitarray.");


static PyObject *
bitarray_count(bitarrayobject *self, PyObject *args)
{
    PyObject *x = Py_True;
    idx_t start = 0, stop = self->nbits;
    int vi;

    if (!PyArg_ParseTuple(args, "|OLL:count", &x, &start, &stop))
        return NULL;

    vi = PyObject_IsTrue(x);
    if (vi < 0)
        return NULL;

    normalize_index(self->nbits, &start);
    normalize_index(self->nbits, &stop);

    return PyLong_FromLongLong(count(self, vi, start, stop));
}

PyDoc_STRVAR(count_doc,
"count(value=True, start=0, stop=<end of array>, /) -> int\n\
\n\
Count the number of occurrences of bool(value) in the bitarray.");


static PyObject *
bitarray_index(bitarrayobject *self, PyObject *args)
{
    PyObject *x;
    idx_t i, start = 0, stop = self->nbits;
    int vi;

    if (!PyArg_ParseTuple(args, "O|LL:index", &x, &start, &stop))
        return NULL;

    vi = PyObject_IsTrue(x);
    if (vi < 0)
        return NULL;

    normalize_index(self->nbits, &start);
    normalize_index(self->nbits, &stop);

    i = findfirst(self, vi, start, stop);
    if (i < 0) {
        PyErr_Format(PyExc_ValueError, "%d is not in bitarray", vi);
        return NULL;
    }
    return PyLong_FromLongLong(i);
}

PyDoc_STRVAR(index_doc,
"index(value, start=0, stop=<end of array>, /) -> int\n\
\n\
Return index of the first occurrence of `bool(value)` in the bitarray.\n\
Raises `ValueError` if the value is not present.");


static PyObject *
bitarray_extend(bitarrayobject *self, PyObject *obj)
{
    if (extend_dispatch(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(iterable or string, /)\n\
\n\
Extend bitarray by appending the truth value of each element given\n\
by iterable.  If a string is provided, each `0` and `1` are appended\n\
as bits.");


static PyObject *
bitarray_contains(bitarrayobject *self, PyObject *x)
{
    long res;

    if (IS_INT_OR_BOOL(x)) {
        int vi;

        vi = IntBool_AsInt(x);
        if (vi < 0)
            return NULL;
        res = findfirst(self, vi, 0, self->nbits) >= 0;
    }
    else if (bitarray_Check(x)) {
        res = search(self, (bitarrayobject *) x, 0) >= 0;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "bitarray or bool expected");
        return NULL;
    }
    return PyBool_FromLong(res);
}

PyDoc_STRVAR(contains_doc,
"__contains__(value, /) -> bool\n\
\n\
Return True if bitarray contains value, False otherwise.\n\
The value may be a boolean (or integer between 0 and 1), or a bitarray.");


static PyObject *
bitarray_search(bitarrayobject *self, PyObject *args)
{
    PyObject *list = NULL;   /* list of matching positions to be returned */
    PyObject *x, *item = NULL;
    Py_ssize_t limit = -1;
    bitarrayobject *xa;
    idx_t p;

    if (!PyArg_ParseTuple(args, "O|n:_search", &x, &limit))
        return NULL;

    if (!bitarray_Check(x)) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected for search");
        return NULL;
    }
    xa = (bitarrayobject *) x;
    if (xa->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        return NULL;
    }
    list = PyList_New(0);
    if (list == NULL)
        return NULL;
    if (xa->nbits > self->nbits || limit == 0)
        return list;

    p = 0;
    while (1) {
        p = search(self, xa, p);
        if (p < 0)
            break;
        item = PyLong_FromLongLong(p);
        p++;
        if (item == NULL || PyList_Append(list, item) < 0) {
            Py_XDECREF(item);
            Py_XDECREF(list);
            return NULL;
        }
        Py_DECREF(item);
        if (limit > 0 && PyList_Size(list) >= limit)
            break;
    }
    return list;
}

PyDoc_STRVAR(search_doc,
"search(bitarray, limit=<none>, /) -> list\n\
\n\
Searches for the given bitarray in self, and return the list of start\n\
positions.\n\
The optional argument limits the number of search results to the integer\n\
specified.  By default, all search results are returned.");


static PyObject *
bitarray_buffer_info(bitarrayobject *self)
{
    PyObject *res, *ptr;

    ptr = PyLong_FromVoidPtr(self->ob_item),
    res = Py_BuildValue("OLsiL",
                        ptr,
                        (idx_t) Py_SIZE(self),
                        ENDIAN_OBJ(self),
                        (int) (BITS(Py_SIZE(self)) - self->nbits),
                        (idx_t) self->allocated);
    Py_DECREF(ptr);
    return res;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info() -> tuple\n\
\n\
Return a tuple (address, size, endianness, unused, allocated) giving the\n\
memory address of the bitarray's data, the size (in bytes) used to hold the\n\
bitarray's contents, the bit endianness as a string, the number of unused\n\
bits in the last bytes, and the size (in bytes) of the allocated memory.");


static PyObject *
bitarray_endian(bitarrayobject *self)
{
    return Py_BuildValue("s", ENDIAN_OBJ(self));
}

PyDoc_STRVAR(endian_doc,
"endian() -> str\n\
\n\
Return the bit endianness as a string (either `little` or `big`).");


static PyObject *
bitarray_append(bitarrayobject *self, PyObject *v)
{
    if (append_item(self, v) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(append_doc,
"append(item, /)\n\
\n\
Append the truth value `bool(item)` to the end of the bitarray.");


static PyObject *
bitarray_all(bitarrayobject *self)
{
    return PyBool_FromLong(findfirst(self, 0, 0, self->nbits) == -1);
}

PyDoc_STRVAR(all_doc,
"all() -> bool\n\
\n\
Returns True when all bits in the array are True.");


static PyObject *
bitarray_any(bitarrayobject *self)
{
    return PyBool_FromLong(findfirst(self, 1, 0, self->nbits) >= 0);
}

PyDoc_STRVAR(any_doc,
"any() -> bool\n\
\n\
Returns True when any bit in the array is True.");


static PyObject *
bitarray_reduce(bitarrayobject *self)
{
    PyObject *dict, *repr = NULL, *result = NULL;
    Py_ssize_t nbytes = Py_SIZE(self);
    char *data;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }
    /* the first byte indicates the number of unused bits at the end, and
       the rest of the bytes consist of the raw binary data */
    data = (char *) PyMem_Malloc(nbytes + 1);
    if (data == NULL) {
        PyErr_NoMemory();
        goto error;
    }
    data[0] = (char) setunused(self);
    memcpy(data + 1, self->ob_item, nbytes);
    repr = PyBytes_FromStringAndSize(data, nbytes + 1);
    if (repr == NULL)
        goto error;
    PyMem_Free((void *) data);
    result = Py_BuildValue("O(Os)O", Py_TYPE(self),
                           repr, ENDIAN_OBJ(self), dict);
error:
    Py_DECREF(dict);
    Py_XDECREF(repr);
    return result;
}

PyDoc_STRVAR(reduce_doc, "state information for pickling");


static PyObject *
bitarray_reverse(bitarrayobject *self)
{
    const idx_t m = self->nbits - 1;   /* index of max item of self */
    bitarrayobject *t;    /* temp bitarray to store lower half of self */
    idx_t i;

    if (self->nbits < 2)        /* nothing needs to be done */
        Py_RETURN_NONE;

    t = (bitarrayobject *) newbitarrayobject(Py_TYPE(self),
                                             self->nbits / 2, self->endian);
    if (t == NULL)
        return NULL;

    /* copy lower half of array into temporary array */
    memcpy(t->ob_item, self->ob_item, Py_SIZE(t));

    /* reverse the upper half onto the lower half. */
    for (i = 0; i < t->nbits; i++)
        setbit(self, i, GETBIT(self, m - i));

    /* revert the stored away lower half onto the upper half. */
    for (i = 0; i < t->nbits; i++)
        setbit(self, m - i, GETBIT(t, i));

    bitarray_dealloc(t);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(reverse_doc,
"reverse()\n\
\n\
Reverse the order of bits in the array (in-place).");


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
Adds zeros to the end of the bitarray, such that the length of the bitarray\n\
will be a multiple of 8.  Returns the number of bits added (0..7).");


static PyObject *
bitarray_invert(bitarrayobject *self)
{
    Py_ssize_t n = Py_SIZE(self), i;

    for (i = 0; i < n; i++)
        self->ob_item[i] = ~self->ob_item[i];
    Py_RETURN_NONE;
}

PyDoc_STRVAR(invert_doc,
"invert()\n\
\n\
Invert all bits in the array (in-place),\n\
i.e. convert each 1-bit into a 0-bit and vice versa.");


static PyObject *
bitarray_bytereverse(bitarrayobject *self)
{
    static char trans[256];
    static int setup = 0;
    Py_ssize_t i;

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
    for (i = 0; i < Py_SIZE(self); i++)
        self->ob_item[i] = trans[(unsigned char) self->ob_item[i]];

    Py_RETURN_NONE;
}

PyDoc_STRVAR(bytereverse_doc,
"bytereverse()\n\
\n\
For all bytes representing the bitarray, reverse the bit order (in-place).\n\
Note: This method changes the actual machine values representing the\n\
bitarray; it does not change the endianness of the bitarray object.");


static PyObject *
bitarray_setall(bitarrayobject *self, PyObject *v)
{
    int vi;

    vi = PyObject_IsTrue(v);
    if (vi < 0)
        return NULL;

    memset(self->ob_item, vi ? 0xff : 0x00, Py_SIZE(self));
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setall_doc,
"setall(value, /)\n\
\n\
Set all bits in the bitarray to `bool(value)`.");


static PyObject *
bitarray_sort(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    idx_t n, n0, n1;
    int reverse = 0;
    static char *kwlist[] = {"reverse", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    n = self->nbits;
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
    PyObject *list;
    idx_t i;

    list = PyList_New((Py_ssize_t) self->nbits);
    if (list == NULL)
        return NULL;

    for (i = 0; i < self->nbits; i++)
        if (PyList_SetItem(list, (Py_ssize_t) i,
                           PyBool_FromLong(GETBIT(self, i))) < 0)
            return NULL;
    return list;
}

PyDoc_STRVAR(tolist_doc,
"tolist() -> list\n\
\n\
Return a list with the items (False or True) in the bitarray.\n\
Note that the list object being created will require 32 or 64 times more\n\
memory (depending on the machine architecture) than the bitarray object,\n\
which may cause a memory error if the bitarray is very large.");


static PyObject *
bitarray_frombytes(bitarrayobject *self, PyObject *bytes)
{
    Py_ssize_t nbytes;
    idx_t t, p;

    if (!PyBytes_Check(bytes)) {
        PyErr_SetString(PyExc_TypeError, "bytes expected");
        return NULL;
    }
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
           PyBytes_AsString(bytes), nbytes);

    if (delete_n(self, t, p) < 0)
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
bits (1..7) are considered to be 0.");


static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *bytes, *f, *res;
    Py_ssize_t nblock, nread = 0, nbytes = -1;
    int not_enough_bytes;

    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &nbytes))
        return NULL;

    if (nbytes == 0)
        Py_RETURN_NONE;

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
When n is provided and positions but exceeds the data available,\n\
EOFError is raised (but the available data is still read and appended.");


static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    Py_ssize_t size, nbytes = Py_SIZE(self);
    Py_ssize_t offset;
    PyObject *res;

    if (nbytes == 0)
        Py_RETURN_NONE;

    setunused(self);
    for (offset = 0; offset < nbytes; offset += BLOCKSIZE) {
        size = Py_MIN(nbytes - offset, BLOCKSIZE);
        assert(size >=0 && offset + size <= nbytes);
        res = PyObject_CallMethod(f, "write",
                                  PY_MAJOR_VERSION == 2 ? "s#" : "y#",
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
When the length of the bitarray is not a multiple of 8,\n\
the remaining bits (1..7) are set to 0.");


static PyObject *
bitarray_to01(bitarrayobject *self)
{
    return unpack(self, '0', '1', OUT_to01);
}

PyDoc_STRVAR(to01_doc,
"to01() -> str\n\
\n\
Return a string containing `0`s and `1`s, representing the bits in the\n\
bitarray object.");


static PyObject *
bitarray_unpack(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    char zero = 0x00, one = 0xff;
    static char *kwlist[] = {"zero", "one", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    return unpack(self, zero, one, OUT_unpack);
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\xff') -> bytes\n\
\n\
Return bytes containing one character for each bit in the bitarray,\n\
using the specified mapping.");


static PyObject *
bitarray_pack(bitarrayobject *self, PyObject *bytes)
{
    if (!PyBytes_Check(bytes)) {
        PyErr_SetString(PyExc_TypeError, "bytes expected");
        return NULL;
    }
    if (extend_bytes(self, bytes, BYTES_RAW) < 0)
        return NULL;

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
bitarray_repr(bitarrayobject *self)
{
    if (self->nbits == 0)
        return Py_BuildValue("s", "bitarray()");

    return unpack(self, '0', '1', OUT_repr);
}


static PyObject *
bitarray_insert(bitarrayobject *self, PyObject *args)
{
    idx_t i;
    PyObject *v;

    if (!PyArg_ParseTuple(args, "LO:insert", &i, &v))
        return NULL;

    normalize_index(self->nbits, &i);

    if (insert_n(self, i, 1) < 0)
        return NULL;
    if (set_item(self, i, v) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(insert_doc,
"insert(index, value, /)\n\
\n\
Insert `bool(value)` into the bitarray before index.");


static PyObject *
bitarray_pop(bitarrayobject *self, PyObject *args)
{
    idx_t i = -1;
    long vi;

    if (!PyArg_ParseTuple(args, "|L:pop", &i))
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
    return PyBool_FromLong(vi);
}

PyDoc_STRVAR(pop_doc,
"pop(index=-1, /) -> item\n\
\n\
Return the i-th (default last) element and delete it from the bitarray.\n\
Raises `IndexError` if bitarray is empty or index is out of range.");


static PyObject *
bitarray_remove(bitarrayobject *self, PyObject *v)
{
    idx_t i;
    int vi;

    vi = PyObject_IsTrue(v);
    if (vi < 0)
        return NULL;

    i = findfirst(self, vi, 0, self->nbits);
    if (i < 0) {
        PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);
        return NULL;
    }
    if (delete_n(self, i, 1) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(remove_doc,
"remove(value, /)\n\
\n\
Remove the first occurrence of `bool(value)` in the bitarray.\n\
Raises `ValueError` if item is not present.");


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


/* --------- special methods ----------- */

static PyObject *
bitarray_getitem(bitarrayobject *self, PyObject *a)
{
    PyObject *res;
    idx_t start, stop, step, slicelength, j, i = 0;

    if (IS_INDEX(a)) {
        if (getIndex(a, &i) < 0)
            return NULL;
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
            return NULL;
        }
        return PyBool_FromLong(GETBIT(self, i));
    }
    if (PySlice_Check(a)) {
        if (slice_GetIndicesEx((PySliceObject *) a, self->nbits,
                               &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }
        res = newbitarrayobject(Py_TYPE(self), slicelength, self->endian);
        if (res == NULL)
            return NULL;

        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit((bitarrayobject *) res, i, GETBIT(self, j));

        return res;
    }
    PyErr_SetString(PyExc_TypeError, "index or slice expected");
    return NULL;
}

/* Sets the elements, specified by slice, in self to the value(s) given by v
   which is either a bitarray or a boolean. */
static int
setslice(bitarrayobject *self, PySliceObject *slice, PyObject *v)
{
    idx_t start, stop, step, slicelength, j, i = 0;

    if (slice_GetIndicesEx(slice, self->nbits,
                           &start, &stop, &step, &slicelength) < 0)
        return -1;

    if (bitarray_Check(v)) {
#define vv  ((bitarrayobject *) v)
        if (vv->nbits == slicelength) {
            for (i = 0, j = start; i < slicelength; i++, j += step)
                setbit(self, j, GETBIT(vv, i));
            return 0;
        }
        if (step != 1) {
            PyErr_Format(PyExc_ValueError,
                         "attempt to assign sequence of size %lld "
                         "to extended slice of size %lld",
                         vv->nbits, (idx_t) slicelength);
            return -1;
        }
        /* make self bigger or smaller */
        if (vv->nbits > slicelength) {
            if (insert_n(self, start, vv->nbits - slicelength) < 0)
                return -1;
        }
        else {
            if (delete_n(self, start, slicelength - vv->nbits) < 0)
                return -1;
        }
        /* copy the new values into self */
        copy_n(self, start, vv, 0, vv->nbits);
#undef vv
        return 0;
    }
    if (IS_INT_OR_BOOL(v)) {
        int vi;

        vi = IntBool_AsInt(v);
        if (vi < 0)
            return -1;
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, vi);
        return 0;
    }
    PyErr_SetString(PyExc_IndexError,
                    "bitarray or bool expected for slice assignment");
    return -1;
}

static PyObject *
bitarray_setitem(bitarrayobject *self, PyObject *args)
{
    PyObject *a, *v;
    idx_t i = 0;

    if (!PyArg_ParseTuple(args, "OO:__setitem__", &a, &v))
        return NULL;

    if (IS_INDEX(a)) {
        if (getIndex(a, &i) < 0)
            return NULL;
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
            return NULL;
        }
        if (set_item(self, i, v) < 0)
            return NULL;
        Py_RETURN_NONE;
    }
    if (PySlice_Check(a)) {
        if (setslice(self, (PySliceObject *) a, v) < 0)
            return NULL;
        Py_RETURN_NONE;
    }
    PyErr_SetString(PyExc_TypeError, "index or slice expected");
    return NULL;
}

static PyObject *
bitarray_delitem(bitarrayobject *self, PyObject *a)
{
    idx_t start, stop, step, slicelength, j, i = 0;

    if (IS_INDEX(a)) {
        if (getIndex(a, &i) < 0)
            return NULL;
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
            return NULL;
        }
        if (delete_n(self, i, 1) < 0)
            return NULL;
        Py_RETURN_NONE;
    }
    if (PySlice_Check(a)) {
        if (slice_GetIndicesEx((PySliceObject *) a, self->nbits,
                               &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }
        if (slicelength == 0)
            Py_RETURN_NONE;

        if (step < 0) {
            stop = start + 1;
            start = stop + step * (slicelength - 1) - 1;
            step = -step;
        }
        if (step == 1) {
            assert(stop - start == slicelength);
            if (delete_n(self, start, slicelength) < 0)
                return NULL;
            Py_RETURN_NONE;
        }
        /* this is the only complicated part when step > 1 */
        for (i = j = start; i < self->nbits; i++)
            if ((i - start) % step != 0 || i >= stop) {
                setbit(self, j, GETBIT(self, i));
                j++;
            }
        if (resize(self, self->nbits - slicelength) < 0)
            return NULL;
        Py_RETURN_NONE;
    }
    PyErr_SetString(PyExc_TypeError, "index or slice expected");
    return NULL;
}

/* ---------- number methods ---------- */

static PyObject *
bitarray_add(bitarrayobject *self, PyObject *other)
{
    PyObject *res;

    res = bitarray_copy(self);
    if (extend_dispatch((bitarrayobject *) res, other) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

static PyObject *
bitarray_iadd(bitarrayobject *self, PyObject *other)
{
    if (extend_dispatch(self, other) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
bitarray_mul(bitarrayobject *self, PyObject *v)
{
    PyObject *res;
    idx_t vi = 0;

    if (!IS_INDEX(v)) {
        PyErr_SetString(PyExc_TypeError,
                        "integer value expected for bitarray repetition");
        return NULL;
    }
    if (getIndex(v, &vi) < 0)
        return NULL;
    res = bitarray_copy(self);
    if (repeat((bitarrayobject *) res, vi) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

static PyObject *
bitarray_imul(bitarrayobject *self, PyObject *v)
{
    idx_t vi = 0;

    if (!IS_INDEX(v)) {
        PyErr_SetString(PyExc_TypeError,
            "integer value expected for in-place bitarray repetition");
        return NULL;
    }
    if (getIndex(v, &vi) < 0)
        return NULL;
    if (repeat(self, vi) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
bitarray_cpinvert(bitarrayobject *self)
{
    PyObject *res;

    res = bitarray_copy(self);
    bitarray_invert((bitarrayobject *) res);
    return res;
}

#define BITWISE_FUNC(oper)  \
static PyObject *                                                   \
bitarray_ ## oper (bitarrayobject *self, PyObject *other)           \
{                                                                   \
    PyObject *res;                                                  \
                                                                    \
    res = bitarray_copy(self);                                      \
    if (bitwise((bitarrayobject *) res, other, OP_ ## oper) < 0) {  \
        Py_DECREF(res);                                             \
        return NULL;                                                \
    }                                                               \
    return res;                                                     \
}

BITWISE_FUNC(and)
BITWISE_FUNC(or)
BITWISE_FUNC(xor)


#define BITWISE_IFUNC(oper)  \
static PyObject *                                            \
bitarray_i ## oper (bitarrayobject *self, PyObject *other)   \
{                                                            \
    if (bitwise(self, other, OP_ ## oper) < 0)               \
        return NULL;                                         \
    Py_INCREF(self);                                         \
    return (PyObject *) self;                                \
}

BITWISE_IFUNC(and)
BITWISE_IFUNC(or)
BITWISE_IFUNC(xor)

/******************* variable length encoding and decoding ***************/

static int
check_codedict(PyObject *codedict)
{
    PyObject *key, *value;
    Py_ssize_t pos = 0;

    if (!PyDict_Check(codedict)) {
        PyErr_SetString(PyExc_TypeError, "dict expected");
        return -1;
    }
    if (PyDict_Size(codedict) == 0) {
        PyErr_SetString(PyExc_ValueError, "prefix code dict empty");
        return -1;
    }
    while (PyDict_Next(codedict, &pos, &key, &value)) {
        if (!bitarray_Check(value)) {
            PyErr_SetString(PyExc_TypeError,
                            "bitarray expected for dict value");
            return -1;
        }
        if (((bitarrayobject *) value)->nbits == 0) {
            PyErr_SetString(PyExc_ValueError, "non-empty bitarray expected");
            return -1;
        }
    }
    return 0;
}

static PyObject *
bitarray_encode(bitarrayobject *self, PyObject *args)
{
    PyObject *codedict, *iterable, *iter, *symbol, *bits;

    if (!PyArg_ParseTuple(args, "OO:encode", &codedict, &iterable))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    iter = PyObject_GetIter(iterable);
    if (iter == NULL) {
        PyErr_SetString(PyExc_TypeError, "iterable object expected");
        return NULL;
    }
    /* extend self with the bitarrays from codedict */
    while ((symbol = PyIter_Next(iter)) != NULL) {
        bits = PyDict_GetItem(codedict, symbol);
        Py_DECREF(symbol);
        if (bits == NULL) {
            PyErr_SetString(PyExc_ValueError,
                            "symbol not defined in prefix code");
            goto error;
        }
        if (extend_bitarray(self, (bitarrayobject *) bits) < 0)
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


/* Binary tree definition */
typedef struct _bin_node
{
    struct _bin_node *child[2];
    PyObject *symbol;
} binode;


static binode *
new_binode(void)
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
delete_binode_tree(binode *tree)
{
    if (tree == NULL)
        return;

    delete_binode_tree(tree->child[0]);
    delete_binode_tree(tree->child[1]);
    PyMem_Free(tree);
}

/* insert symbol (mapping to ba) into the tree */
static int
insert_symbol(binode *tree, bitarrayobject *ba, PyObject *symbol)
{
    binode *nd = tree, *prev;
    Py_ssize_t i;
    int k;

    for (i = 0; i < ba->nbits; i++) {
        k = GETBIT(ba, i);
        prev = nd;
        nd = nd->child[k];

        /* we cannot have already a symbol when branching to the new leaf */
        if (nd && nd->symbol)
            goto ambiguity;

        if (!nd) {
            nd = new_binode();
            if (nd == NULL)
                return -1;
            prev->child[k] = nd;
        }
    }
    /* the new leaf node cannot already have a symbol or children */
    if (nd->symbol || nd->child[0] || nd->child[1])
        goto ambiguity;

    nd->symbol = symbol;
    return 0;

 ambiguity:
    PyErr_SetString(PyExc_ValueError, "prefix code ambiguous");
    return -1;
}

/* return a binary tree from a codedict, which is created by inserting
   all symbols mapping to bitarrays */
static binode *
make_tree(PyObject *codedict)
{
    binode *tree;
    PyObject *symbol, *array;
    Py_ssize_t pos = 0;

    tree = new_binode();
    if (tree == NULL)
        return NULL;

    while (PyDict_Next(codedict, &pos, &symbol, &array)) {
        if (insert_symbol(tree, (bitarrayobject *) array, symbol) < 0) {
            delete_binode_tree(tree);
            return NULL;
        }
    }
    return tree;
}

/* Traverse tree using the branches corresponding to the bitarray `ba`,
   starting at *indexp.  Return the symbol at the leaf node, or NULL
   when the end of the bitarray has been reached, or on error (in which
   case the appropriate PyErr_SetString is set.
*/
static PyObject *
traverse_tree(binode *tree, bitarrayobject *ba, idx_t *indexp)
{
    binode *nd = tree;
    int k;

    while (*indexp < ba->nbits) {
        k = GETBIT(ba, *indexp);
        (*indexp)++;
        nd = nd->child[k];
        if (nd == NULL) {
            PyErr_SetString(PyExc_ValueError,
                            "prefix code does not match data in bitarray");
            return NULL;
        }
        if (nd->symbol)  /* leaf */
            return nd->symbol;
    }
    if (nd != tree)
        PyErr_SetString(PyExc_ValueError, "decoding not terminated");

    return NULL;
}

static PyObject *
bitarray_decode(bitarrayobject *self, PyObject *codedict)
{
    binode *tree, *nd;
    PyObject *list = NULL;
    Py_ssize_t i;
    int k;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = make_tree(codedict);
    if (tree == NULL || PyErr_Occurred())
        goto error;

    nd = tree;
    list = PyList_New(0);
    if (list == NULL)
        goto error;

    /* traverse tree (just like above) */
    for (i = 0; i < self->nbits; i++) {
        k = GETBIT(self, i);
        nd = nd->child[k];
        if (nd == NULL) {
            PyErr_SetString(PyExc_ValueError,
                            "prefix code does not match data in bitarray");
            goto error;
        }
        if (nd->symbol) {  /* leaf */
            if (PyList_Append(list, nd->symbol) < 0)
                goto error;
            nd = tree;
        }
    }
    if (nd != tree) {
        PyErr_SetString(PyExc_ValueError, "decoding not terminated");
        goto error;
    }
    delete_binode_tree(tree);
    return list;

error:
    delete_binode_tree(tree);
    Py_XDECREF(list);
    return NULL;
}

PyDoc_STRVAR(decode_doc,
"decode(code, /) -> list\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
decode the content of the bitarray and return it as a list of symbols.");

/*********************** (Bitarray) Decode Iterator *********************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're searching in */
    binode *tree;               /* prefix tree containing symbols */
    idx_t index;                /* current index in bitarray */
} decodeiterobject;

static PyTypeObject DecodeIter_Type;

#define DecodeIter_Check(op)  PyObject_TypeCheck(op, &DecodeIter_Type)


/* create a new initialized bitarray decode iterator object */
static PyObject *
bitarray_iterdecode(bitarrayobject *self, PyObject *codedict)
{
    decodeiterobject *it;       /* iterator to be returned */
    binode *tree;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = make_tree(codedict);
    if (tree == NULL || PyErr_Occurred())
        return NULL;

    it = PyObject_GC_New(decodeiterobject, &DecodeIter_Type);
    if (it == NULL)
        return NULL;

    it->tree = tree;

    Py_INCREF(self);
    it->bao = self;
    it->index = 0;
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(iterdecode_doc,
"iterdecode(code, /) -> iterator\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
decode the content of the bitarray and return an iterator over\n\
the symbols.");

static PyObject *
decodeiter_next(decodeiterobject *it)
{
    PyObject *symbol;

    assert(DecodeIter_Check(it));
    symbol = traverse_tree(it->tree, it->bao, &(it->index));
    if (symbol == NULL)  /* stop iteration OR error occured */
        return NULL;
    Py_INCREF(symbol);
    return symbol;
}

static void
decodeiter_dealloc(decodeiterobject *it)
{
    delete_binode_tree(it->tree);
    PyObject_GC_UnTrack(it);
    Py_XDECREF(it->bao);
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
    "bitarraydecodeiterator",                 /* tp_name */
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

/*********************** (Bitarray) Search Iterator *********************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're searching in */
    bitarrayobject *xa;         /* bitarray being searched for */
    idx_t p;                    /* current search position */
} searchiterobject;

static PyTypeObject SearchIter_Type;

#define SearchIter_Check(op)  PyObject_TypeCheck(op, &SearchIter_Type)

/* create a new initialized bitarray search iterator object */
static PyObject *
bitarray_itersearch(bitarrayobject *self, PyObject *x)
{
    searchiterobject *it;  /* iterator to be returned */
    bitarrayobject *xa;

    if (!bitarray_Check(x)) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected for itersearch");
        return NULL;
    }
    xa = (bitarrayobject *) x;
    if (xa->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        return NULL;
    }

    it = PyObject_GC_New(searchiterobject, &SearchIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->bao = self;
    Py_INCREF(xa);
    it->xa = xa;
    it->p = 0;  /* start search at position 0 */
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(itersearch_doc,
"itersearch(bitarray, /) -> iterator\n\
\n\
Searches for the given a bitarray in self, and return an iterator over\n\
the start positions where bitarray matches self.");

static PyObject *
searchiter_next(searchiterobject *it)
{
    idx_t p;

    assert(SearchIter_Check(it));
    p = search(it->bao, it->xa, it->p);
    if (p < 0)  /* no more positions -- stop iteration */
        return NULL;
    it->p = p + 1;  /* next search position */
    return PyLong_FromLongLong(p);
}

static void
searchiter_dealloc(searchiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_XDECREF(it->bao);
    Py_XDECREF(it->xa);
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
    "bitarraysearchiterator",                 /* tp_name */
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

/*************************** Method definitions *************************/

static PyMethodDef
bitarray_methods[] = {
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
    {"fromfile",     (PyCFunction) bitarray_fromfile,    METH_VARARGS,
     fromfile_doc},
    {"frombytes",    (PyCFunction) bitarray_frombytes,   METH_O,
     frombytes_doc},
    {"index",        (PyCFunction) bitarray_index,       METH_VARARGS,
     index_doc},
    {"insert",       (PyCFunction) bitarray_insert,      METH_VARARGS,
     insert_doc},
    {"invert",       (PyCFunction) bitarray_invert,      METH_NOARGS,
     invert_doc},
    {"length",       (PyCFunction) bitarray_length,      METH_NOARGS,
     length_doc},
    {"pack",         (PyCFunction) bitarray_pack,        METH_O,
     pack_doc},
    {"pop",          (PyCFunction) bitarray_pop,         METH_VARARGS,
     pop_doc},
    {"remove",       (PyCFunction) bitarray_remove,      METH_O,
     remove_doc},
    {"reverse",      (PyCFunction) bitarray_reverse,     METH_NOARGS,
     reverse_doc},
    {"setall",       (PyCFunction) bitarray_setall,      METH_O,
     setall_doc},
    {"search",       (PyCFunction) bitarray_search,      METH_VARARGS,
     search_doc},
    {"itersearch",   (PyCFunction) bitarray_itersearch,  METH_O,
     itersearch_doc},
    {"sort",         (PyCFunction) bitarray_sort,        METH_VARARGS |
                                                         METH_KEYWORDS,
     sort_doc},
    {"tofile",       (PyCFunction) bitarray_tofile,      METH_O,
     tofile_doc},
    {"tolist",       (PyCFunction) bitarray_tolist,      METH_NOARGS,
     tolist_doc},
    {"tobytes",      (PyCFunction) bitarray_tobytes,     METH_NOARGS,
     tobytes_doc},
    {"to01",         (PyCFunction) bitarray_to01,        METH_NOARGS,
     to01_doc},
    {"unpack",       (PyCFunction) bitarray_unpack,      METH_VARARGS |
                                                         METH_KEYWORDS,
     unpack_doc},

    /* special methods */
    {"__copy__",     (PyCFunction) bitarray_copy,        METH_NOARGS,
     copy_doc},
    {"__deepcopy__", (PyCFunction) bitarray_copy,        METH_O,
     copy_doc},
    {"__len__",      (PyCFunction) bitarray_length,      METH_NOARGS,
     len_doc},
    {"__contains__", (PyCFunction) bitarray_contains,    METH_O,
     contains_doc},
    {"__reduce__",   (PyCFunction) bitarray_reduce,      METH_NOARGS,
     reduce_doc},

    /* slice methods */
    {"__delitem__",  (PyCFunction) bitarray_delitem,     METH_O,       0},
    {"__getitem__",  (PyCFunction) bitarray_getitem,     METH_O,       0},
    {"__setitem__",  (PyCFunction) bitarray_setitem,     METH_VARARGS, 0},

    /* number methods */
    {"__add__",      (PyCFunction) bitarray_add,         METH_O,       0},
    {"__iadd__",     (PyCFunction) bitarray_iadd,        METH_O,       0},
    {"__mul__",      (PyCFunction) bitarray_mul,         METH_O,       0},
    {"__rmul__",     (PyCFunction) bitarray_mul,         METH_O,       0},
    {"__imul__",     (PyCFunction) bitarray_imul,        METH_O,       0},
    {"__and__",      (PyCFunction) bitarray_and,         METH_O,       0},
    {"__or__",       (PyCFunction) bitarray_or,          METH_O,       0},
    {"__xor__",      (PyCFunction) bitarray_xor,         METH_O,       0},
    {"__iand__",     (PyCFunction) bitarray_iand,        METH_O,       0},
    {"__ior__",      (PyCFunction) bitarray_ior,         METH_O,       0},
    {"__ixor__",     (PyCFunction) bitarray_ixor,        METH_O,       0},
    {"__invert__",   (PyCFunction) bitarray_cpinvert,    METH_NOARGS,  0},

    {NULL,           NULL}  /* sentinel */
};


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

    PyErr_SetString(PyExc_ValueError,
                    "bit endianness must be either 'little' or 'big'");
    return -1;
}


static PyObject *
bitarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *a;  /* to be returned in some cases */
    PyObject *initial = NULL;
    char *endian_str = NULL;
    int endian;
    static char *kwlist[] = {"", "endian", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                        "|Os:bitarray", kwlist, &initial, &endian_str))
        return NULL;

    endian = endian_from_string(endian_str);
    if (endian < 0)
        return NULL;

    /* no arg or None */
    if (initial == NULL || initial == Py_None)
        return newbitarrayobject(type, 0, endian);

    /* int, long */
    if (IS_INDEX(initial)) {
        idx_t nbits;

        if (getIndex(initial, &nbits) < 0)
            return NULL;
        if (nbits < 0) {
            PyErr_SetString(PyExc_ValueError,
                            "cannot create bitarray with negative length");
            return NULL;
        }
        return newbitarrayobject(type, nbits, endian);
    }

    /* from bitarray itself */
    if (bitarray_Check(initial)) {
#define np  ((bitarrayobject *) initial)
        a = newbitarrayobject(type, np->nbits,
                              endian_str == NULL ? np->endian : endian);
        if (a == NULL)
            return NULL;
        memcpy(((bitarrayobject *) a)->ob_item, np->ob_item, Py_SIZE(np));
#undef np
        return a;
    }

    /* bytes (for pickling) */
    if (PyBytes_Check(initial)) {
        Py_ssize_t nbytes;
        char *data;

        nbytes = PyBytes_Size(initial);
        if (nbytes == 0)        /* no bytes */
            return newbitarrayobject(type, 0, endian);

        data = PyBytes_AsString(initial);
        if (0 <= data[0] && data[0] < 8) {
            /* when the first character is smaller than 8, it indicates the
               number of unused bits at the end, and rest of the bytes
               consist of the raw binary data */
            if (nbytes == 1 && data[0] > 0) {
                PyErr_Format(PyExc_ValueError,
                             "did not expect 0x0%d", (int) data[0]);
                return NULL;
            }
            a = newbitarrayobject(type, BITS(nbytes - 1) - ((idx_t) data[0]),
                                  endian);
            if (a == NULL)
                return NULL;
            memcpy(((bitarrayobject *) a)->ob_item, data + 1, nbytes - 1);
            return a;
        }
    }

    /* leave remaining type dispatch to the extend method */
    a = newbitarrayobject(type, 0, endian);
    if (a == NULL)
        return NULL;
    if (extend_dispatch((bitarrayobject *) a, initial) < 0) {
        Py_DECREF(a);
        return NULL;
    }
    return a;
}


static PyObject *
richcompare(PyObject *v, PyObject *w, int op)
{
    int cmp, vi, wi;
    idx_t i, vs, ws;

    if (!bitarray_Check(v) || !bitarray_Check(w)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
#define va  ((bitarrayobject *) v)
#define wa  ((bitarrayobject *) w)
    vs = va->nbits;
    ws = wa->nbits;
    if (vs != ws) {
        /* shortcut for EQ/NE: if sizes differ, the bitarrays differ */
        if (op == Py_EQ)
            Py_RETURN_FALSE;
        if (op == Py_NE)
            Py_RETURN_TRUE;
    }

    /* to avoid uninitialized warning for some compilers */
    vi = wi = 0;
    /* search for the first index where items are different */
    for (i = 0; i < vs && i < ws; i++) {
        vi = GETBIT(va, i);
        wi = GETBIT(wa, i);
        if (vi != wi) {
            /* we have an item that differs -- first, shortcut for EQ/NE */
            if (op == Py_EQ)
                Py_RETURN_FALSE;
            if (op == Py_NE)
                Py_RETURN_TRUE;
            /* compare the final item using the proper operator */
            switch (op) {
            case Py_LT: cmp = vi <  wi; break;
            case Py_LE: cmp = vi <= wi; break;
            case Py_EQ: cmp = vi == wi; break;
            case Py_NE: cmp = vi != wi; break;
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

/************************** Bitarray Iterator **************************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *bao;        /* bitarray we're iterating over */
    idx_t index;                /* current index in bitarray */
} bitarrayiterobject;

static PyTypeObject BitarrayIter_Type;

#define BitarrayIter_Check(op)  PyObject_TypeCheck(op, &BitarrayIter_Type)

/* create a new initialized bitarray iterator object, this object is
   returned when calling iter(a) */
static PyObject *
bitarray_iter(bitarrayobject *self)
{
    bitarrayiterobject *it;

    assert(bitarray_Check(self));
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

    assert(BitarrayIter_Check(it));
    if (it->index < it->bao->nbits) {
        vi = GETBIT(it->bao, it->index);
        it->index++;
        return PyBool_FromLong(vi);
    }
    return NULL;  /* stop iteration */
}

static void
bitarrayiter_dealloc(bitarrayiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_XDECREF(it->bao);
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
    "bitarrayiterator",                       /* tp_name */
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

/********************* Bitarray Buffer Interface ************************/
#ifdef WITH_BUFFER

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
    void *ptr;

    if (view == NULL) {
        self->ob_exports++;
        return 0;
    }
    ptr = (void *) self->ob_item;
    ret = PyBuffer_FillInfo(view, (PyObject *) self, ptr,
                            Py_SIZE(self), 0, flags);
    if (ret >= 0) {
        self->ob_exports++;
    }
    return ret;
}

static void
bitarray_releasebuffer(bitarrayobject *self, Py_buffer *view)
{
    self->ob_exports--;
}

static PyBufferProcs bitarray_as_buffer = {
#if PY_MAJOR_VERSION == 2   /* old buffer protocol */
    (readbufferproc) bitarray_buffer_getreadbuf,
    (writebufferproc) bitarray_buffer_getwritebuf,
    (segcountproc) bitarray_buffer_getsegcount,
    (charbufferproc) bitarray_buffer_getcharbuf,
#endif
    (getbufferproc) bitarray_getbuffer,
    (releasebufferproc) bitarray_releasebuffer,
};

#endif  /* WITH_BUFFER */

/************************** Bitarray Type *******************************/

PyDoc_STRVAR(bitarraytype_doc,
"_bitarray(initializer=0, /, endian='big') -> bitarray\n\
\n\
Return a new bitarray object.\n\
");

static PyTypeObject Bitarraytype = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray._bitarray",                     /* tp_name */
    sizeof(bitarrayobject),                   /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) bitarray_dealloc,            /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    (reprfunc) bitarray_repr,                 /* tp_repr */
    0,                                        /* tp_as_number*/
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
#ifdef WITH_BUFFER
    &bitarray_as_buffer,                      /* tp_as_buffer */
#else
    0,                                        /* tp_as_buffer */
#endif
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_WEAKREFS
#if defined(WITH_BUFFER) && PY_MAJOR_VERSION == 2
    | Py_TPFLAGS_HAVE_NEWBUFFER
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

/*************************** Module functions **********************/

static PyObject *
bitdiff(PyObject *module, PyObject *args)
{
    PyObject *a, *b;
    Py_ssize_t i;
    idx_t res = 0;
    unsigned char c;

    if (!PyArg_ParseTuple(args, "OO:bitdiff", &a, &b))
        return NULL;
    if (!(bitarray_Check(a) && bitarray_Check(b))) {
        PyErr_SetString(PyExc_TypeError, "bitarray object expected");
        return NULL;
    }

#define aa  ((bitarrayobject *) a)
#define bb  ((bitarrayobject *) b)
    if (aa->nbits != bb->nbits) {
        PyErr_SetString(PyExc_ValueError,
                        "bitarrays of equal length expected");
        return NULL;
    }
    setunused(aa);
    setunused(bb);
    for (i = 0; i < Py_SIZE(aa); i++) {
        c = aa->ob_item[i] ^ bb->ob_item[i];
        res += bitcount_lookup[c];
    }
#undef aa
#undef bb
    return PyLong_FromLongLong(res);
}

PyDoc_STRVAR(bitdiff_doc,
"bitdiff(a, b, /) -> int\n\
\n\
Return the difference between two bitarrays a and b.\n\
This is function does the same as (a ^ b).count(), but is more memory\n\
efficient, as no intermediate bitarray object gets created.\n\
Deprecated since version 1.2.0, use `bitarray.util.count_xor()` instead.");


static PyObject *
bits2bytes(PyObject *module, PyObject *v)
{
    idx_t n = 0;

    if (!IS_INDEX(v)) {
        PyErr_SetString(PyExc_TypeError, "integer expected");
        return NULL;
    }
    if (getIndex(v, &n) < 0)
        return NULL;
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "non-negative integer expected");
        return NULL;
    }
    return PyLong_FromLongLong(BYTES(n));
}

PyDoc_STRVAR(bits2bytes_doc,
"bits2bytes(n, /) -> int\n\
\n\
Return the number of bytes necessary to store n bits.");


static PyObject *
get_default_endian(PyObject *module)
{
    return Py_BuildValue("s", ENDIAN_INT(default_endian));
}

PyDoc_STRVAR(get_default_endian_doc,
"get_default_endian() -> string\n\
\n\
Return the default endianness for new bitarray objects being created.\n\
Under normal circumstances, the return value is `big`.");


static PyObject *
set_default_endian(PyObject *module, PyObject *args)
{
    char *endian_str;
    int tmp;

    if (!PyArg_ParseTuple(args, "s:_set_default_endian", &endian_str))
        return NULL;

    /* As endian_from_string might return -1, we have to store its value
       in a temporary variable before setting default_endian. */
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
    return Py_BuildValue("iiiiL",
                         (int) sizeof(void *),
                         (int) sizeof(size_t),
                         (int) sizeof(Py_ssize_t),
                         (int) sizeof(idx_t),
                         (idx_t) PY_SSIZE_T_MAX);
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo() -> tuple\n\
\n\
tuple(sizeof(void *),\n\
      sizeof(size_t),\n\
      sizeof(Py_ssize_t),\n\
      sizeof(idx_t),\n\
      PY_SSIZE_T_MAX)");

/*
   In retrospect, I wish I had never added any modules functions (other
   than get_default_endian() and _set_default_endian()) here.
   These, and possibly many others should be part of a separate utility
   module.  Anyway, at this point (2019) it is too late to remove them,
   so I will just leave them here, but not any new ones.
*/
static PyMethodDef module_functions[] = {
    {"bitdiff",    (PyCFunction) bitdiff,    METH_VARARGS, bitdiff_doc   },
    {"bits2bytes", (PyCFunction) bits2bytes, METH_O,       bits2bytes_doc},
    {"get_default_endian", (PyCFunction) get_default_endian, METH_NOARGS,
                                                   get_default_endian_doc},
    {"_set_default_endian", (PyCFunction) set_default_endian, METH_VARARGS,
                                                   set_default_endian_doc},
    {"_sysinfo",   (PyCFunction) sysinfo,    METH_NOARGS,  sysinfo_doc   },
    {NULL,         NULL}  /* sentinel */
};

/*********************** Install Module **************************/

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

    Py_TYPE(&Bitarraytype) = &PyType_Type;
    Py_TYPE(&SearchIter_Type) = &PyType_Type;
    Py_TYPE(&DecodeIter_Type) = &PyType_Type;
    Py_TYPE(&BitarrayIter_Type) = &PyType_Type;
#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
    if (m == NULL)
        return NULL;
#else
    m = Py_InitModule3("_bitarray", module_functions, 0);
    if (m == NULL)
        return;
#endif

    Py_INCREF((PyObject *) &Bitarraytype);
    PyModule_AddObject(m, "_bitarray", (PyObject *) &Bitarraytype);
    PyModule_AddObject(m, "__version__",
                       Py_BuildValue("s", BITARRAY_VERSION));
#ifdef IS_PY3K
    return m;
#endif
}
