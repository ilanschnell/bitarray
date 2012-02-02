/*
   This file is the C part of the bitarray package.  Almost all
   functionality is implemented here.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

#ifdef IS_PY3K
#include "bytesobject.h"
#define PyString_FromStringAndSize  PyBytes_FromStringAndSize
#define PyString_FromString  PyBytes_FromString
#define PyString_Check  PyBytes_Check
#define PyString_Size  PyBytes_Size
#define PyString_AsString  PyBytes_AsString
#define PyString_ConcatAndDel  PyBytes_ConcatAndDel
#define Py_TPFLAGS_HAVE_WEAKREFS  0
#endif

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 6
/* Backward compatibility with Python 2.5 */
#define Py_TYPE(ob)   (((PyObject *) (ob))->ob_type)
#define Py_SIZE(ob)   (((PyVarObject *) (ob))->ob_size)
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#else  /* !STDC_HEADERS */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      /* For size_t */
#endif /* HAVE_SYS_TYPES_H */
#endif /* !STDC_HEADERS */


typedef long long int idx_t;

/* throughout:  0 = little endian   1 = big endian */
#define DEFAULT_ENDIAN  1

typedef struct {
    PyObject_VAR_HEAD
    char *ob_item;
    Py_ssize_t allocated;
    idx_t nbits;
    int endian;
    PyObject *weakreflist;   /* List of weak references */
} bitarrayobject;

static PyTypeObject Bitarraytype;

#define bitarray_Check(obj)  PyObject_TypeCheck(obj, &Bitarraytype)

#define BITS(bytes)  (((idx_t) 8) * ((idx_t) (bytes)))

#define BYTES(bits)  (((bits) == 0) ? 0 : (((bits) - 1) / 8 + 1))

#define BITMASK(endian, i)  (((char) 1) << ((endian) ? (7 - (i)%8) : (i)%8))

/* ------------ low level access to bits in bitarrayobject ------------- */

#define GETBIT(self, i)  \
    ((self)->ob_item[(i) / 8] & BITMASK((self)->endian, i) ? 1 : 0)

static void
setbit(bitarrayobject *self, idx_t i, int bit)
{
    char *cp, mask;

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
    idx_t max_bits;

    assert (nbits >= 0);
    if (sizeof(void *) == 4) {  /* 32bit machine */
        max_bits = ((idx_t) 1) << 34;  /* 2^34 = 16 Gbits*/
        if (nbits > max_bits) {
            char buff[256];
            sprintf(buff, "cannot create bitarray of size %lld, "
                          "max size is %lld", nbits, max_bits);
            PyErr_SetString(PyExc_OverflowError, buff);
            return -1;
        }
    }
    return 0;
}

static int
resize(bitarrayobject *self, idx_t nbits)
{
    Py_ssize_t newsize;
    size_t _new_size;       /* for allocation */

    if (check_overflow(nbits) < 0)
        return -1;

    newsize = (Py_ssize_t) BYTES(nbits);

    /* Bypass realloc() when a previous overallocation is large enough
       to accommodate the newsize.  If the newsize is 16 smaller than the
       current size, then proceed with the realloc() to shrink the list.
    */
    if (self->allocated >= newsize &&
        Py_SIZE(self) < newsize + 16 &&
        self->ob_item != NULL)
    {
        Py_SIZE(self) = newsize;
        self->nbits = nbits;
        return 0;
    }

    if (newsize >= Py_SIZE(self) + 65536)
        /* Don't overallocate when the size increase is very large. */
        _new_size = newsize;
    else
        /* This over-allocates proportional to the bitarray size, making
           room for additional growth.  The over-allocation is mild, but is
           enough to give linear-time amortized behavior over a long
           sequence of appends() in the presence of a poorly-performing
           system realloc().
           The growth pattern is:  0, 4, 8, 16, 25, 34, 44, 54, 65, 77, ...
           Note, the pattern starts out the same as for lists but then
           grows at a smaller rate so that larger bitarrays only overallocate
           by about 1/16th -- this is done because bitarrays are assumed
           to be memory critical.
        */
        _new_size = (newsize >> 4) + (Py_SIZE(self) < 8 ? 3 : 7) + newsize;

    self->ob_item = PyMem_Realloc(self->ob_item, _new_size);
    if (self->ob_item == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    Py_SIZE(self) = newsize;
    self->allocated = _new_size;
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
    obj->nbits = nbits;
    obj->endian = endian;
    if (nbytes == 0) {
        obj->ob_item = NULL;
    }
    else {
        obj->ob_item = PyMem_Malloc((size_t) nbytes);
        if (obj->ob_item == NULL) {
            PyObject_Del(obj);
            PyErr_NoMemory();
            return NULL;
        }
    }
    obj->allocated = nbytes;
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

    assert (a >= 0 && b >= 0 && n >= 0);
    assert (a + n <= self->nbits);
    assert (b + n <= other->nbits);

    /* XXX
    if (self->endian == other->endian && a % 8 == 0 && b % 8 == 0 && n >= 8)
    {
        Py_ssize_t bytes;
        idx_t bits;

        bytes = n / 8;
        bits = 8 * bytes;
        copy_n(self, bits + a, other, bits + b, n - bits);
        memmove(self->ob_item + a / 8, other->ob_item + b / 8, bytes);
        return;
    }
    */

    /* the different type of looping is only relevant when other and self
       are the same object, i.e. when copying a piece of an bitarrayobject
       onto itself */
    if (a < b) {
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
    assert (start >= 0 && start <= self->nbits);
    assert (n >= 0 && start + n <= self->nbits);
    if (n == 0)
        return 0;

    copy_n(self, start, self, start + n, self->nbits - start - n);
    return resize(self, self->nbits - n);
}

/* starting at start, insert n (uninitialized) bits into self */
static int
insert_n(bitarrayobject *self, idx_t start, idx_t n)
{
    assert (start >= 0 && start <= self->nbits);
    assert (n >= 0);
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
    idx_t i, n;
    int res = 0;

    n = BITS(Py_SIZE(self));
    for (i = self->nbits; i < n; i++) {
        setbit(self, i, 0);
        res++;
    }
    assert (res < 8);
    return res;
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
    OP_xor
};

/* perform bitwise operation */
static int
bitwise(bitarrayobject *self, PyObject *arg, enum op_type oper)
{
    bitarrayobject *other;
    Py_ssize_t i;

    if (!bitarray_Check(arg)) {
        PyErr_SetString(PyExc_TypeError,
                        "bitarray objects expected for bitwise operation");
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
        for (i = 0; i < Py_SIZE(self); i++)
            self->ob_item[i] &= other->ob_item[i];
        break;
    case OP_or:
        for (i = 0; i < Py_SIZE(self); i++)
            self->ob_item[i] |= other->ob_item[i];
        break;
    case OP_xor:
        for (i = 0; i < Py_SIZE(self); i++)
            self->ob_item[i] ^= other->ob_item[i];
        break;
    }
    return 0;
}

/* set the bits from start to stop (excluding) in self to val */
static void
setrange(bitarrayobject *self, idx_t start, idx_t stop, int val)
{
    idx_t i;

    assert (start <= self->nbits && stop <= self->nbits);
    for (i = start; i < stop; i++)
        setbit(self, i, val);
}

static void
invert(bitarrayobject *self)
{
    Py_ssize_t i;

    for (i = 0; i < Py_SIZE(self); i++)
        self->ob_item[i] = ~self->ob_item[i];
}

/* reverse the order of bits in each byte of the buffer */
static void
bytereverse(bitarrayobject *self)
{
    static char trans[256];
    static int setup = 0;
    Py_ssize_t i;
    unsigned char c;

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
    for (i = 0; i < Py_SIZE(self); i++) {
        c = self->ob_item[i];
        self->ob_item[i] = trans[c];
    }
}

/* returns number of 1 bits */
static idx_t
count(bitarrayobject *self)
{
    static int bitcount[256];
    static int setup = 0;
    Py_ssize_t i;
    idx_t res = 0;
    unsigned char c;

    if (!setup) {
        /* setup lookup table, which maps each byte to it's bit count:
           bitcount = {0, 1, 1, 2, 1, 2, 2, 3, 1, ..., 8} */
        int j, k;
        for (k = 0; k < 256; k++) {
            bitcount[k] = 0;
            for (j = 0; j < 8; j++)
                bitcount[k] += (k >> j) & 1;
        }
        setup = 1;
    }

    setunused(self);
    for (i = 0; i < Py_SIZE(self); i++) {
        c = self->ob_item[i];
        res += bitcount[c];
    }
    return res;
}

/* return index of first occurrence of vi, -1 when x is not in found. */
static idx_t
findfirst(bitarrayobject *self, int vi, idx_t start, idx_t stop)
{
    Py_ssize_t j;
    idx_t i;
    char c;

    if (Py_SIZE(self) == 0)
        return -1;
    if (start < 0 || start > self->nbits)
        start = 0;
    if (stop < 0 || stop > self->nbits)
        stop = self->nbits;
    if (start >= stop)
        return -1;

    if (stop > start + 8) {
        /* seraching for 1 means: break when byte is not 0x00
           searching for 0 means: break when byte is not 0xff */
        c = vi ? 0x00 : 0xff;

        /* skip ahead by checking whole bytes */
        for (j = (Py_ssize_t) (start / 8); j < BYTES(stop); j++)
            if (c ^ self->ob_item[j])
                break;

        if (j == Py_SIZE(self))
            j--;
        assert (j >= 0 && j < Py_SIZE(self));

        if (start < BITS(j))
            start = BITS(j);
    }

    /* fine grained search */
    for (i = start; i < stop; i++)
        if (GETBIT(self, i) == vi)
            return i;

    return -1;
}

static int
set_item(bitarrayobject *self, idx_t i, PyObject *v)
{
    long vi;

    assert (i >= 0 && i < self->nbits);
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

static PyObject *
unpack(bitarrayobject *self, char zero, char one)
{
    PyObject *res;
    Py_ssize_t i;
    char *str;

    if (self->nbits > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "bitarray too large to unpack");
        return NULL;
    }
    str = PyMem_Malloc((size_t) self->nbits);
    if (str == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    for (i = 0; i < self->nbits; i++) {
        *(str + i) = GETBIT(self, i) ? one : zero;
    }
    res = PyString_FromStringAndSize(str, (Py_ssize_t) self->nbits);
    PyMem_Free((void *) str);
    return res;
}

static int
extend_bitarray(bitarrayobject *self, bitarrayobject *other)
{
    idx_t sumbits;

    if (other->nbits == 0)
        return 0;

    sumbits = self->nbits + other->nbits;
    if (resize(self, sumbits) < 0)
        return -1;

    copy_n(self, sumbits - other->nbits, other, 0, other->nbits);
    return 0;
}

static int
extend_iter(bitarrayobject *self, PyObject *iter)
{
    PyObject *item;

    assert (PyIter_Check(iter));
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
    Py_ssize_t addbits, i;

    assert (PyList_Check(list));
    addbits = PyList_Size(list);
    if (addbits == 0)
        return 0;

    if (resize(self, self->nbits + addbits) < 0)
        return -1;

    for (i = 0; i < addbits; i++) {
        item = PyList_GetItem(list, i);
        if (item == NULL)
            return -1;
        set_item(self, self->nbits - addbits + i, item);
    }
    return 0;
}

static int
extend_tuple(bitarrayobject *self, PyObject *tuple)
{
    PyObject *item;
    Py_ssize_t addbits, i;

    assert (PyTuple_Check(tuple));
    addbits = PyTuple_Size(tuple);
    if (addbits == 0)
        return 0;

    if (resize(self, self->nbits + addbits) < 0)
        return -1;

    for (i = 0; i < addbits; i++) {
        item = PyTuple_GetItem(tuple, i);
        if (item == NULL)
            return -1;
        set_item(self, self->nbits - addbits + i, item);
    }
    return 0;
}

/* extend_string(): extend the bitarray from a string, where each whole
   characters is converted to a single bit
*/
enum conv_tp {
    STR_01,    /*  '0' -> 0    '1'  -> 1   no other characters allowed */
    STR_RAW,   /*  0x00 -> 0   other -> 1                              */
};

static int
extend_string(bitarrayobject *self, PyObject *string, enum conv_tp conv)
{
    Py_ssize_t strlen, i;
    char c, *str;
    int vi = 0;

    assert (PyString_Check(string));
    strlen = PyString_Size(string);
    if (strlen == 0)
        return 0;

    if (resize(self, self->nbits + strlen) < 0)
        return -1;

    str = PyString_AsString(string);

    for (i = 0; i < strlen; i++) {
        c = *(str + i);
        /* depending on conv, map c to bit */
        switch (conv) {
        case STR_01:
            switch (c) {
            case '0': vi = 0; break;
            case '1': vi = 1; break;
            default:
                PyErr_Format(PyExc_ValueError,
                             "character must be '0' or '1', found '%c'", c);
                return -1;
            }
            break;
        case STR_RAW:
            vi = c ? 1 : 0;
            break;
        }
        setbit(self, self->nbits - strlen + i, vi);
    }
    return 0;
}

static int
extend_rawstring(bitarrayobject *self, PyObject *string)
{
    Py_ssize_t strlen;
    char *str;

    assert (PyString_Check(string) && self->nbits % 8 == 0);
    strlen = PyString_Size(string);
    if (strlen == 0)
        return 0;

    if (resize(self, self->nbits + BITS(strlen)) < 0)
        return -1;

    str = PyString_AsString(string);
    memcpy(self->ob_item + (Py_SIZE(self) - strlen), str, strlen);
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

    if (PyString_Check(obj))                                 /* str01 */
        return extend_string(self, obj, STR_01);

#ifdef IS_PY3K
    if (PyUnicode_Check(obj)) {                               /* str01 */
        PyObject *string;
        string = PyUnicode_AsEncodedString(obj, NULL, NULL);
        ret = extend_string(self, string, STR_01);
        Py_DECREF(string);
        return ret;
    }
#endif

    if (PyIter_Check(obj))                                    /* iter */
        return extend_iter(self, obj);

    /* finally, try to get the iterator of the object */
    iter = PyObject_GetIter(obj);
    if (iter == NULL) {
        PyErr_SetString(PyExc_TypeError, "could not extend bitarray");
        return -1;
    }
    ret = extend_iter(self, iter);
    Py_DECREF(iter);
    return ret;
}

/* --------- helper functions not involving bitarrayobjects ------------ */

#define ENDIANSTR(x)  ((x) ? "big" : "little")

#ifdef IS_PY3K
#define ISINDEX(x)  (PyLong_Check(x) || PyIndex_Check(x))
#else
#define ISINDEX(x)  (PyInt_Check(x) || PyLong_Check(x) || PyIndex_Check(x))
#endif

/* Extract a slice index from a PyInt or PyLong or an object with the
   nb_index slot defined, and store in *i.  Return 0 on error, 1 on success.

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
            return 0;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "slice indices must be integers or "
                                         "None or have an __index__ method");
        return 0;
    }
    *i = x;
    return 1;
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
        if (!getIndex(r->step, step))
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
        if (!getIndex(r->start, start)) return -1;
        if (*start < 0) *start += length;
        if (*start < 0) *start = (*step < 0) ? -1 : 0;
        if (*start >= length) *start = (*step < 0) ? length - 1 : length;
    }

    if (r->stop == Py_None) {
        *stop = defstop;
    }
    else {
        if (!getIndex(r->stop, stop)) return -1;
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
                         Implementation of API methods
 **************************************************************************/

static PyObject *
bitarray_length(bitarrayobject *self)
{
    return PyLong_FromLongLong(self->nbits);
}

PyDoc_STRVAR(length_doc,
"length()\n\
\n\
Return the length, i.e. number of bits stored in the bitarray.\n\
This method is preferred over __len__, [used when typing ``len(a)``],\n\
since __len__ will fail for a bitarray object with 2^31 or more elements\n\
on a 32bit machine, whereas this method will return the correct value,\n\
on 32bit and 64bit machines.");

PyDoc_STRVAR(len_doc,
"__len__()\n\
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
"copy()\n\
\n\
Return a copy of the bitarray.");


static PyObject *
bitarray_count(bitarrayobject *self, PyObject *args)
{
    idx_t n1;
    long x = 1;

    if (!PyArg_ParseTuple(args, "|i:count", &x))
        return NULL;

    n1 = count(self);
    return PyLong_FromLongLong(x ? n1 : (self->nbits - n1));
}

PyDoc_STRVAR(count_doc,
"count([x])\n\
\n\
Return number of occurrences of x in the bitarray.  x defaults to True.");


static PyObject *
bitarray_index(bitarrayobject *self, PyObject *args)
{
    PyObject *x;
    idx_t i, start = 0, stop = -1;
    long vi;

    if (!PyArg_ParseTuple(args, "O|LL:index", &x, &start, &stop))
        return NULL;

    vi = PyObject_IsTrue(x);
    if (vi < 0)
        return NULL;

    i = findfirst(self, vi, start, stop);
    if (i < 0) {
        PyErr_SetString(PyExc_ValueError, "index(x): x not in bitarray");
        return NULL;
    }
    return PyLong_FromLongLong(i);
}

PyDoc_STRVAR(index_doc,
"index(x, [start, [stop]])\n\
\n\
Return index of the first occurrence of x in the bitarray.\n\
It is an error when x does not occur in the bitarray");


static PyObject *
bitarray_extend(bitarrayobject *self, PyObject *obj)
{
    if (extend_dispatch(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(object)\n\
\n\
Append bits to the end of the bitarray.  The objects which can be passed\n\
to this method are the same iterable objects which can given to a bitarray\n\
object upon initialization.");


static PyObject *
bitarray_search(bitarrayobject *self, PyObject *args)
{
    PyObject *list = NULL;   /* list of matching positions to be returned */
    PyObject *x, *item = NULL;
    Py_ssize_t limit;
    bitarrayobject *xa;
    idx_t p, n;

    if (!PyArg_ParseTuple(args, "On:_search", &x, &limit))
        return NULL;

    assert(bitarray_Check(x));
    xa = (bitarrayobject *) x;

    if (xa->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        goto error;
    }
    list = PyList_New(0);
    if (xa->nbits > self->nbits || limit == 0)
        return list;

    for (p = 0; p < self->nbits - xa->nbits + 1; p++) {
        for (n = 0; n < xa->nbits; n++)
            if (GETBIT(self, p + n) != GETBIT(xa, n))
                goto next;

        /* we have a match, append the position to the list */
        item = PyLong_FromLongLong(p);
        if (item == NULL || PyList_Append(list, item) < 0)
            goto error;

        Py_DECREF(item);
        if (limit > 0 && PyList_Size(list) >= limit)
            break;
    next:
        ; /* do nothing */
    }
    return list;
error:
    Py_XDECREF(item);
    Py_XDECREF(list);
    return NULL;
}

PyDoc_STRVAR(search_doc,
"_search(x, limit)  like search but x has to be a bitarray.");


static PyObject *
bitarray_search_at(bitarrayobject *self, PyObject *args)
{
    PyObject *x;
    bitarrayobject *xa;
    idx_t p, n;

    if (!PyArg_ParseTuple(args, "OL:_search_at", &x, &p))
        return NULL;

    assert(bitarray_Check(x));
    xa = (bitarrayobject *) x;

    if (xa->nbits == 0) {
        PyErr_SetString(PyExc_ValueError, "can't search for empty bitarray");
        return NULL;
    }
    if (xa->nbits > self->nbits)
        Py_RETURN_NONE;

    if (p < 0) {
        PyErr_SetString(PyExc_ValueError, "positive start value expected");
        return NULL;
    }

    for (; p < self->nbits - xa->nbits + 1; p++) {
        for (n = 0; n < xa->nbits; n++)
            if (GETBIT(self, p + n) != GETBIT(xa, n))
                goto next;

        /* we have a match, return the position */
        return PyLong_FromLongLong(p);
    next:
        ; /* do nothing */
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(search_at_doc,
"_search_at(x, start)  search for bitarray x starting at start, and return\n\
the index where the bitarray x is found (or None if x is not found).");


static PyObject *
bitarray_buffer_info(bitarrayobject *self)
{
    PyObject *res, *ptr;

    ptr = PyLong_FromVoidPtr(self->ob_item),
    res = Py_BuildValue("OLsiL",
                        ptr,
                        (idx_t) Py_SIZE(self),
                        ENDIANSTR(self->endian),
                        (int) (BITS(Py_SIZE(self)) - self->nbits),
                        (idx_t) self->allocated);

    Py_DECREF(ptr);
    return res;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info()\n\
\n\
Return a tuple (address, size, endianness, unused, allocated) giving the\n\
current memory address, the size (in bytes) used to hold the bitarray's\n\
contents, the bit endianness as a string, the number of unused bits\n\
(e.g. a bitarray of length 11 will have a buffer size of 2 bytes and\n\
5 unused bits), and the size (in bytes) of the allocated memory.");


static PyObject *
bitarray_endian(bitarrayobject *self)
{
#ifdef IS_PY3K
    return PyUnicode_FromString(ENDIANSTR(self->endian));
#else
    return PyString_FromString(ENDIANSTR(self->endian));
#endif
}

PyDoc_STRVAR(endian_doc,
"endian()\n\
\n\
Return the bit endianness as a string (either 'little' or 'big').");


static PyObject *
bitarray_append(bitarrayobject *self, PyObject *v)
{
    if (append_item(self, v) < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(append_doc,
"append(x)\n\
\n\
Append the value bool(x) to the end of the bitarray.");


static PyObject *
bitarray_all(bitarrayobject *self)
{
    if (findfirst(self, 0, 0, -1) >= 0)
        Py_RETURN_FALSE;
    else
        Py_RETURN_TRUE;
}

PyDoc_STRVAR(all_doc,
"all()\n\
\n\
Returns True when all bits in the array are True.");


static PyObject *
bitarray_any(bitarrayobject *self)
{
    if (findfirst(self, 1, 0, -1) >= 0)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

PyDoc_STRVAR(any_doc,
"any()\n\
\n\
Returns True when any bit in the array is True.");


static PyObject *
bitarray_reduce(bitarrayobject *self)
{
    PyObject *dict, *unpacked, *result = NULL;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }
    unpacked = unpack(self, '0', '1');
    if (unpacked == NULL)
        goto error;

    result = Py_BuildValue("O(Os)O",
                           Py_TYPE(self),
                           unpacked,
                           ENDIANSTR(self->endian),
                           dict);
error:
    Py_DECREF(dict);
    Py_XDECREF(unpacked);
    return result;
}

PyDoc_STRVAR(reduce_doc, "Return state information for pickling.");


static PyObject *
bitarray_reverse(bitarrayobject *self)
{
    PyObject *t;    /* temp bitarray to store lower half of self */
    idx_t i, m;

    if (self->nbits < 2)
        Py_RETURN_NONE;

    t = newbitarrayobject(Py_TYPE(self), self->nbits / 2, self->endian);
    if (t == NULL)
        return NULL;

#define tt  ((bitarrayobject *) t)
    /* copy lower half of array into temporary array */
    memcpy(tt->ob_item, self->ob_item, Py_SIZE(tt));

    m = self->nbits - 1;

    /* reverse the upper half onto the lower half. */
    for (i = 0; i < tt->nbits; i++)
        setbit(self, i, GETBIT(self, m - i));

    /* revert the stored away lower half onto the upper half. */
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
bitarray_fill(bitarrayobject *self)
{
    long p;

    p = setunused(self);
    self->nbits += p;
#ifdef IS_PY3K
    return PyLong_FromLong(p);
#else
    return PyInt_FromLong(p);
#endif
}

PyDoc_STRVAR(fill_doc,
"fill()\n\
\n\
Returns the number of bits added (0..7) at the end of the array.\n\
When the length of the bitarray is not a multiple of 8, increase the length\n\
slightly such that the new length is a multiple of 8, and set the few new\n\
bits to False.");


static PyObject *
bitarray_invert(bitarrayobject *self)
{
    invert(self);
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
    bytereverse(self);
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
    long vi;

    vi = PyObject_IsTrue(v);
    if (vi < 0)
        return NULL;

    memset(self->ob_item, vi ? 0xff : 0x00, Py_SIZE(self));
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setall_doc,
"setall(x)\n\
\n\
Set all bits in the bitarray to bool(x).");


static PyObject *
bitarray_sort(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    idx_t n, n0, n1;
    int reverse = 0;
    static char* kwlist[] = {"reverse", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    n = self->nbits;
    n1 = count(self);

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


#ifdef IS_PY3K
static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *f;
    Py_ssize_t newsize, nbytes = -1;
    PyObject *reader, *rargs, *result;
    size_t nread;
    idx_t t, p;

    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &nbytes))
        return NULL;

    if (nbytes == 0)
        Py_RETURN_NONE;

    reader = PyObject_GetAttrString(f, "read");
    if (reader == NULL)
    {
        PyErr_SetString(PyExc_TypeError,
                        "first argument must be an open file");
        return NULL;
    }
    rargs = Py_BuildValue("(n)", nbytes);
    if (rargs == NULL) {
        Py_DECREF(reader);
        return NULL;
    }
    result = PyEval_CallObject(reader, rargs);
    if (result != NULL) {
        if (!PyBytes_Check(result)) {
            PyErr_SetString(PyExc_TypeError,
                            "first argument must be an open file");
            Py_DECREF(result);
            Py_DECREF(rargs);
            Py_DECREF(reader);
            return NULL;
        }

        nread = PyBytes_Size(result);

        t = self->nbits;
        p = setunused(self);
        self->nbits += p;

        newsize = Py_SIZE(self) + nread;

        if (resize(self, BITS(newsize)) < 0) {
            Py_DECREF(result);
            Py_DECREF(rargs);
            Py_DECREF(reader);
            return NULL;
        }

        memcpy(self->ob_item + (Py_SIZE(self) - nread),
               PyBytes_AS_STRING(result), nread);

        if (nbytes > 0 && nread < (size_t) nbytes) {
            PyErr_SetString(PyExc_EOFError, "not enough items read");
            return NULL;
        }
        if (delete_n(self, t, p) < 0)
            return NULL;
        Py_DECREF(result);
    }

    Py_DECREF(rargs);
    Py_DECREF(reader);

    Py_RETURN_NONE;
}
#else
static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *f;
    FILE *fp;
    Py_ssize_t newsize, nbytes = -1;
    size_t nread;
    idx_t t, p;
    long cur;

    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &nbytes))
        return NULL;

    fp = PyFile_AsFile(f);
    if (fp == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "first argument must be an open file");
        return NULL;
    }

    /* find number of bytes till EOF */
    if (nbytes < 0) {
        if ((cur = ftell(fp)) < 0)
            goto EOFerror;

        if (fseek(fp, 0L, SEEK_END) || (nbytes = ftell(fp)) < 0)
            goto EOFerror;

        nbytes -= cur;
        if (fseek(fp, cur, SEEK_SET)) {
        EOFerror:
            PyErr_SetString(PyExc_EOFError, "could not find EOF");
            return NULL;
        }
    }
    if (nbytes == 0)
        Py_RETURN_NONE;

    /* file exists and there are more than zero bytes to read */
    t = self->nbits;
    p = setunused(self);
    self->nbits += p;

    newsize = Py_SIZE(self) + nbytes;
    if (resize(self, BITS(newsize)) < 0)
        return NULL;

    nread = fread(self->ob_item + (Py_SIZE(self) - nbytes), 1, nbytes, fp);
    if (nread < (size_t) nbytes) {
        newsize -= nbytes - nread;
        if (resize(self, BITS(newsize)) < 0)
            return NULL;
        PyErr_SetString(PyExc_EOFError, "not enough items in file");
        return NULL;
    }

    if (delete_n(self, t, p) < 0)
        return NULL;
    Py_RETURN_NONE;
}
#endif

PyDoc_STRVAR(fromfile_doc,
"fromfile(f, [n])\n\
\n\
Read n bytes from the file object f and append them to the bitarray\n\
interpreted as machine values.  When n is omitted, as many bytes are\n\
read until EOF is reached.");


#ifdef IS_PY3K
static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    PyObject *writer, *value, *args, *result;

    if (f == NULL) {
        PyErr_SetString(PyExc_TypeError, "writeobject with NULL file");
        return NULL;
    }
    writer = PyObject_GetAttrString(f, "write");
    if (writer == NULL)
        return NULL;
    setunused(self);
    value = PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
    if (value == NULL) {
        Py_DECREF(writer);
        return NULL;
    }
    args = PyTuple_Pack(1, value);
    if (args == NULL) {
        Py_DECREF(value);
        Py_DECREF(writer);
        return NULL;
    }
    result = PyEval_CallObject(writer, args);
    Py_DECREF(args);
    Py_DECREF(value);
    Py_DECREF(writer);
    if (result == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "open file expected");
        return NULL;
    }
    Py_DECREF(result);
    Py_RETURN_NONE;
}
#else
static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    FILE *fp;

    fp = PyFile_AsFile(f);
    if (fp == NULL) {
        PyErr_SetString(PyExc_TypeError, "open file expected");
        return NULL;
    }
    if (Py_SIZE(self) == 0)
        Py_RETURN_NONE;

    setunused(self);
    if (fwrite(self->ob_item, 1, Py_SIZE(self), fp) !=
        (size_t) Py_SIZE(self))
    {
        PyErr_SetFromErrno(PyExc_IOError);
        clearerr(fp);
        return NULL;
    }
    Py_RETURN_NONE;
}
#endif

PyDoc_STRVAR(tofile_doc,
"tofile(f)\n\
\n\
Write all bits (as machine values) to the file object f.\n\
When the length of the bitarray is not a multiple of 8,\n\
the remaining bits (1..7) are set to 0.");


static PyObject *
bitarray_tolist(bitarrayobject *self)
{
    PyObject *list;
    idx_t i;

    list = PyList_New((Py_ssize_t) self->nbits);
    if (list == NULL)
        return NULL;

    for (i = 0; i < self->nbits; i++)
        PyList_SetItem(list, (Py_ssize_t) i,
                       PyBool_FromLong(GETBIT(self, i)));
    return list;
}

PyDoc_STRVAR(tolist_doc,
"tolist()\n\
\n\
Return an ordinary list with the items in the bitarray.\n\
Note: To extend a bitarray with elements from a list,\n\
use the extend method.");


static PyObject *
bitarray_frombytes(bitarrayobject *self, PyObject *string)
{
    idx_t t, p;

    if (!PyString_Check(string)) {
        PyErr_SetString(PyExc_TypeError, "byte string expected");
        return NULL;
    }
    t = self->nbits;
    p = setunused(self);
    self->nbits += p;

    if (extend_rawstring(self, string) < 0)
        return NULL;

    if (delete_n(self, t, p) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(frombytes_doc,
"frombytes(bytes)\n\
\n\
Append from a byte string, interpreted as machine values.");


static PyObject *
bitarray_tobytes(bitarrayobject *self)
{
    setunused(self);
    return PyString_FromStringAndSize(self->ob_item, Py_SIZE(self));
}

PyDoc_STRVAR(tobytes_doc,
"tobytes()\n\
\n\
Return the byte representation of the bitarray.\n\
When the length of the bitarray is not a multiple of 8, the few remaining\n\
bits (1..7) are set to 0.");


static PyObject *
bitarray_to01(bitarrayobject *self)
{
#ifdef IS_PY3K
    PyObject *string, *unpacked;

    unpacked = unpack(self, '0', '1');
    string = PyUnicode_FromEncodedObject(unpacked, NULL, NULL);
    Py_DECREF(unpacked);
    return string;
#else
    return unpack(self, '0', '1');
#endif
}

PyDoc_STRVAR(to01_doc,
"to01()\n\
\n\
Return a string containing '0's and '1's, representing the bits in the\n\
bitarray object.\n\
Note: To extend a bitarray from a string containing '0's and '1's,\n\
use the extend method.");


static PyObject *
bitarray_unpack(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    char zero = 0x00, one = 0xff;
    static char* kwlist[] = {"zero", "one", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    return unpack(self, zero, one);
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\xff')\n\
\n\
Return a byte string containing one character for each bit in the bitarray,\n\
using the specified mapping.\n\
See also the pack method.");


static PyObject *
bitarray_pack(bitarrayobject *self, PyObject *string)
{
    if (!PyString_Check(string)) {
        PyErr_SetString(PyExc_TypeError, "byte string expected");
        return NULL;
    }
    if (extend_string(self, string, STR_RAW) < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(pack_doc,
"pack(bytes)\n\
\n\
Extend the bitarray from a byte string, where each characters corresponds to\n\
a single bit.  The character b'\\x00' maps to bit 0 and all other characters\n\
map to bit 1.\n\
This method, as well as the unpack method, are meant for efficient\n\
transfer of data between bitarray objects to other python objects\n\
(for example NumPy's ndarray object) which have a different view of memory.");


static PyObject *
bitarray_repr(bitarrayobject *self)
{
    PyObject *string;
#ifdef IS_PY3K
    PyObject *decoded;
#endif

    if (self->nbits == 0)
        string = PyString_FromString("bitarray()");
    else
    {
        string = PyString_FromString("bitarray(\'");
        PyString_ConcatAndDel(&string, unpack(self, '0', '1'));
        PyString_ConcatAndDel(&string, PyString_FromString("\')"));
    }
#ifdef IS_PY3K
    decoded = PyUnicode_FromEncodedObject(string, NULL, NULL);
    Py_DECREF(string);
    string = decoded;
#endif
    return string;
}


static PyObject *
bitarray_insert(bitarrayobject *self, PyObject *args)
{
    idx_t i;
    PyObject *v;

    if (!PyArg_ParseTuple(args, "LO:insert", &i, &v))
        return NULL;

    if (i < 0) {
        i += self->nbits;
        if (i < 0)
            i = 0;
    }
    if (i >= self->nbits)
        i = self->nbits;

    if (insert_n(self, i, 1) < 0)
        return NULL;

    set_item(self, i, v);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(insert_doc,
"insert(i, x)\n\
\n\
Insert a new item x into the bitarray before position i.");


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
"pop([i])\n\
\n\
Return the i-th (default last) element and delete it from the bitarray.");


static PyObject *
bitarray_remove(bitarrayobject *self, PyObject *v)
{
    idx_t i;
    long vi;

    vi = PyObject_IsTrue(v);
    if (vi < 0)
        return NULL;

    i = findfirst(self, vi, 0, -1);
    if (i < 0) {
        PyErr_SetString(PyExc_ValueError, "remove(x): x not in bitarray");
        return NULL;
    }
    if (delete_n(self, i, 1) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(remove_doc,
"remove(x)\n\
\n\
Remove the first occurrence of x in the bitarray.");


/* --------- special methods ----------- */

static PyObject *
bitarray_getitem(bitarrayobject *self, PyObject *a)
{
    PyObject *res;
    idx_t start, stop, step, slicelength, j, i = 0;

    if (ISINDEX(a)) {
        getIndex(a, &i);
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
   which is either a bitarray or a boolean.
*/
static int
setslice(bitarrayobject *self, PySliceObject *slice, PyObject *v)
{
    idx_t start, stop, step, slicelength, j, i = 0;
    long vi;

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
            char buff[256];
            sprintf(buff, "attempt to assign sequence of size %lld "
                          "to extended slice of size %lld",
                    vv->nbits, (idx_t) slicelength);
            PyErr_SetString(PyExc_ValueError, buff);
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
    if (PyBool_Check(v)) {
        vi = PyObject_IsTrue(v);
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

    if (ISINDEX(a)) {
        getIndex(a, &i);
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
            return NULL;
        }
        set_item(self, i, v);
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

    if (ISINDEX(a)) {
        getIndex(a, &i);
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
            assert (stop - start == slicelength);
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

    if (!ISINDEX(v)) {
        PyErr_SetString(PyExc_TypeError,
                        "integer value expected for bitarray repetition");
        return NULL;
    }
    getIndex(v, &vi);
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

    if (!ISINDEX(v)) {
        PyErr_SetString(PyExc_TypeError,
            "integer value expected for in-place bitarray repetition");
        return NULL;
    }
    getIndex(v, &vi);
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
    invert((bitarrayobject *) res);
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
                                                             \
    Py_INCREF(self);                                         \
    return (PyObject *) self;                                \
}

BITWISE_IFUNC(and)
BITWISE_IFUNC(or)
BITWISE_IFUNC(xor)

/******************* variable length encoding and decoding ***************/

static PyObject *
bitarray_encode(bitarrayobject *self, PyObject *args)
{
    PyObject *codedict, *iterable, *iter, *symbol, *bits;

    if (!PyArg_ParseTuple(args, "OO:_encode", &codedict, &iterable))
        return NULL;

    iter = PyObject_GetIter(iterable);
    if (iter == NULL) {
        PyErr_SetString(PyExc_TypeError, "iterable object expected");
        return NULL;
    }
    /* extend self with the bitarrays from codedict */
    while ((symbol = PyIter_Next(iter)) != NULL) {
        bits = PyDict_GetItem(codedict, symbol);
        if (bits == NULL) {
            PyErr_SetString(PyExc_ValueError, "symbol not in prefix code");
            Py_DECREF(symbol);
            Py_DECREF(iter);
            return NULL;
        }
        Py_DECREF(symbol);
        if (extend_bitarray(self, (bitarrayobject *) bits) < 0) {
            Py_DECREF(iter);
            return NULL;
        }
    }
    Py_DECREF(iter);
    if (PyErr_Occurred())
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(encode_doc,
"_encode(code, iterable) is like the encode method without code checking");


/* return the leave node resulting from traversing the (binary) tree,
   or, when the iteration is finished, NULL
*/
static PyObject *
tree_traverse(PyObject *iter, PyObject *tree)
{
    PyObject *subtree, *v;
    long vi;

    v = PyIter_Next(iter);
    if (v == NULL)  /* stop iterator */
        return NULL;

    vi = PyObject_IsTrue(v);
    Py_DECREF(v);

    subtree = PyList_GetItem(tree, vi);

    if (PyList_Check(subtree) && PyList_Size(subtree) == 2)
        return tree_traverse(iter, subtree);
    else
        return subtree;
}

#define IS_EMPTY_LIST(x)  (PyList_Check(x) && PyList_Size(x) == 0)

static PyObject *
bitarray_decode(bitarrayobject *self, PyObject *tree)
{
    PyObject *iter = NULL, *symbol, *res;

    iter = PyObject_GetIter((PyObject *) self);
    if (iter == NULL)
        goto error;

    /* traverse binary tree and append symbols to the result list */
    res = PyList_New(0);
    while ((symbol = tree_traverse(iter, tree)) != NULL) {
        if (IS_EMPTY_LIST(symbol)) {
            PyErr_SetString(PyExc_ValueError,
                            "prefix code does not match data in bitarray");
            Py_DECREF(res);
            goto error;
        }
        if (PyList_Append(res, symbol) < 0)
            goto error;
    }
    Py_XDECREF(iter);
    return res;
error:
    Py_XDECREF(iter);
    return NULL;
}

PyDoc_STRVAR(decode_doc,
"_decode(tree)  Given a tree, decode the content of the bitarray and return\n\
the symbols as a list.");


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
    {"copy",         (PyCFunction) bitarray_copy,        METH_NOARGS,
     copy_doc},
    {"count",        (PyCFunction) bitarray_count,       METH_VARARGS,
     count_doc},
    {"_decode",      (PyCFunction) bitarray_decode,      METH_O,
     decode_doc},
    {"_encode",      (PyCFunction) bitarray_encode,      METH_VARARGS,
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
    {"_search",      (PyCFunction) bitarray_search,      METH_VARARGS,
     search_doc},
    {"_search_at",   (PyCFunction) bitarray_search_at,   METH_VARARGS,
     search_at_doc},
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


static PyObject *
bitarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *a;  /* to be returned in some cases */
    PyObject *initial = NULL;
    idx_t nbits = 0;
    char *endianStr = "<NOT_PROVIDED>";
    int endian;
    static char* kwlist[] = {"initial", "endian", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds,
                                     "|Os:bitarray", kwlist,
                                     &initial, &endianStr)) {
        return NULL;
    }
    if (strcmp(endianStr, "little") == 0) {
        endian = 0;
    }
    else if (strcmp(endianStr, "big") == 0) {
        endian = 1;
    }
    else if (strcmp(endianStr, "<NOT_PROVIDED>") == 0) {
        endian = DEFAULT_ENDIAN;  /* use default value */
    }
    else {
        PyErr_SetString(PyExc_ValueError,
                        "endian must be 'little' or 'big'");
        return NULL;
    }

    /* no arg or None */
    if (initial == NULL || initial == Py_None)
        return newbitarrayobject(type, 0, endian);

    /* int, long */
    if (ISINDEX(initial)) {
        getIndex(initial, &nbits);

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
                              (strcmp(endianStr, "<NOT_PROVIDED>") == 0  ?
                               np->endian : endian));
        if (a == NULL)
            return NULL;
        memcpy(((bitarrayobject *) a)->ob_item, np->ob_item, Py_SIZE(np));
#undef np
        return a;
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
    idx_t index;
    bitarrayobject *bao;
} bitarrayiterobject;

static PyTypeObject BitarrayIter_Type;

#define BitarrayIter_Check(op)  PyObject_TypeCheck(op, &BitarrayIter_Type)

static PyObject *
bitarray_iter(bitarrayobject *self)
{
    bitarrayiterobject *it;

    assert (bitarray_Check(self));
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

    assert (BitarrayIter_Check(it));
    if (it->index < it->bao->nbits) {
        vi = GETBIT(it->bao, it->index);
        it->index += 1;
        return PyBool_FromLong(vi);
    }
    return NULL;
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
    PyVarObject_HEAD_INIT(&BitarrayIter_Type, 0)
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

/************************** Bitarray Type *******************************/

static PyTypeObject Bitarraytype = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(&Bitarraytype, 0)
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
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_WEAKREFS,
                                              /* tp_flags */
    0,                                        /* tp_doc */
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
bits2bytes(PyObject *self, PyObject *v)
{
    idx_t n = 0;

    if (!ISINDEX(v)) {
        PyErr_SetString(PyExc_TypeError, "integer expected");
        return NULL;
    }
    getIndex(v, &n);
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "positive value expected");
        return NULL;
    }
    return PyLong_FromLongLong(BYTES(n));
}


PyDoc_STRVAR(bits2bytes_doc,
"bits2bytes(n)\n\
\n\
Return the number of bytes necessary to store n bits.");


static PyObject *
sysinfo(void)
{
    return Py_BuildValue("iiiL",
                         (int) sizeof(void *),
                         (int) sizeof(size_t),
                         (int) sizeof(Py_ssize_t),
                         (idx_t) PY_SSIZE_T_MAX);
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo()\n\
\n\
tuple(sizeof(void *),\n\
      sizeof(size_t),\n\
      sizeof(Py_ssize_t),\n\
      PY_SSIZE_T_MAX)");


static PyMethodDef module_functions[] = {
    {"bits2bytes",  (PyCFunction) bits2bytes,  METH_O,       bits2bytes_doc},
    {"_sysinfo",    (PyCFunction) sysinfo,     METH_NOARGS,  sysinfo_doc   },
    {NULL,          NULL}  /* sentinel */
};

/*********************** Install Module **************************/

#ifdef IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_bitarray", 0, -1, module_functions,
};
PyMODINIT_FUNC
PyInit__bitarray(void)
#else
PyMODINIT_FUNC
init_bitarray(void)
#endif
{
    PyObject *m;

    Py_TYPE(&Bitarraytype) = &PyType_Type;
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
#ifdef IS_PY3K
    return m;
#endif
}
