/*
   Copyright (c) 2019, Ilan Schnell
   bitarray is published under the PSF license.

   This file contains the C implementation of some useful utility functions.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#else  /* !STDC_HEADERS */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>      /* For size_t */
#endif /* HAVE_SYS_TYPES_H */
#endif /* !STDC_HEADERS */


typedef long long int idx_t;

typedef struct {
    PyObject_VAR_HEAD
    char *ob_item;
    Py_ssize_t allocated;       /* how many bytes allocated */
    idx_t nbits;                /* length of bitarray, i.e. elements */
    int endian;                 /* bit endianness of bitarray */
    int ob_exports;             /* how many buffer exports */
    PyObject *weakreflist;      /* list of weak references */
} bitarrayobject;

#define BITS(bytes)  (((idx_t) 8) * ((idx_t) (bytes)))

#define BYTES(bits)  (((bits) == 0) ? 0 : (((bits) - 1) / 8 + 1))

#define BITMASK(endian, i)  (((char) 1) << ((endian) ? (7 - (i)%8) : (i)%8))

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

/* return 1 if obj is a bitarray, 0 otherwise */
static int
bitarray_Check(PyObject *obj)
{
    return PyObject_HasAttrString(obj, "endian");
}

static void
setunused(bitarrayobject *self)
{
    const idx_t n = BITS(Py_SIZE(self));
    idx_t i;

    for (i = self->nbits; i < n; i++)
        setbit(self, i, 0);
}

static int bitcount_lookup[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

/* return index of last occurrence of vi, -1 when x is not in found. */
static idx_t
find_last(bitarrayobject *self, int vi)
{
    Py_ssize_t j;
    idx_t i;
    char c;

    if (self->nbits == 0)
        return -1;

    /* search within top byte */
    for (i = self->nbits - 1; i >= 8 * (self->nbits / 8); i--)
        if (GETBIT(self, i) == vi)
            return i;

    if (i < 0)  /* not found within top byte */
        return -1;
    assert((i + 1) % 8 == 0);

    /* seraching for 1 means: break when byte is not 0x00
       searching for 0 means: break when byte is not 0xff */
    c = vi ? 0x00 : 0xff;

    /* skip ahead by checking whole bytes */
    for (j = BYTES(i) - 1; j >= 0; j--)
        if (c ^ self->ob_item[j])
            break;

    if (j < 0)  /* not found within bytes */
        return -1;

    /* search within byte found */
    for (i = BITS(j + 1) - 1; i >= BITS(j); i--)
        if (GETBIT(self, i) == vi)
            return i;

    return -1;
}

/*************************** Module functions **********************/

static PyObject *
count_n(PyObject *self, PyObject *args)
{
    PyObject *a;
    idx_t n, i = 0, j = 0;
    unsigned char c;
    int k;

    if (!PyArg_ParseTuple(args, "OL:count_n", &a, &n))
        return NULL;

    if (!bitarray_Check(a)) {
        PyErr_SetString(PyExc_TypeError, "bitarray object expected");
        return NULL;
    }
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "non-negative integer expected");
        return NULL;
    }
#define aa  ((bitarrayobject *) a)
    if (n > aa->nbits)  {
        PyErr_SetString(PyExc_ValueError, "n larger than bitarray size");
        return NULL;
    }

    while (j + 16384 < n && i + 16384 < aa->nbits)
        for (k = 0; k < 2048; k++) {
            c = aa->ob_item[(Py_ssize_t) i];
            j += bitcount_lookup[c];
            i++;
        }
    while (j + 8 < n && i + 8 < aa->nbits) {
        c = aa->ob_item[(Py_ssize_t) i];
        j += bitcount_lookup[c];
        i++;
    }
    i *= 8;
    while (j < n && i < aa->nbits ) {
        if (GETBIT(aa, i))
            j++;
        i++;
    }
    if (j < n) {
        PyErr_SetString(PyExc_ValueError, "n exceeds total count");
        return NULL;
    }
#undef aa
    return PyLong_FromLongLong(i);
}

PyDoc_STRVAR(count_n_doc,
"count_n(a, n, /) -> int\n\
\n\
Find the smallest index `i` for which `a[:i].count() == n`.\n\
Raises `ValueError`, when n exceeds the `a.count()`.");


static PyObject *
r_index(PyObject *self, PyObject *args)
{
    PyObject *a, *x = Py_True;
    idx_t i;
    long vi;

    if (!PyArg_ParseTuple(args, "O|O:rindex", &a, &x))
        return NULL;

    if (!bitarray_Check(a)) {
        PyErr_SetString(PyExc_TypeError, "bitarray object expected");
        return NULL;
    }
    vi = PyObject_IsTrue(x);
    if (vi < 0)
        return NULL;

    i = find_last((bitarrayobject *) a, vi);
    if (i < 0) {
        PyErr_SetString(PyExc_ValueError, "index(x): x not in bitarray");
        return NULL;
    }
    return PyLong_FromLongLong(i);
}

PyDoc_STRVAR(rindex_doc,
"rindex(bitarray, value=True, /) -> int\n\
\n\
Return the rightmost index of `bool(value)` in bitarray.\n\
Raises `ValueError` if the value is not present.");


enum kernel_type {
    KERN_cand,     /* count bitwise and -> int */
    KERN_cor,      /* count bitwise or -> int */
    KERN_cxor,     /* count bitwise xor -> int */
    KERN_subset,   /* is subset -> bool */
};

static PyObject *
two_bitarray_func(PyObject *args, enum kernel_type kern, char *format)
{
    PyObject *a, *b;
    Py_ssize_t i;
    idx_t res = 0;
    unsigned char c;

    if (!PyArg_ParseTuple(args, format, &a, &b))
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
    if (aa->endian != bb->endian) {
        PyErr_SetString(PyExc_ValueError,
                        "bitarrays of equal endianness expected");
        return NULL;
    }
    setunused(aa);
    setunused(bb);
    switch (kern) {
    case KERN_cand:
        for (i = 0; i < Py_SIZE(aa); i++) {
            c = aa->ob_item[i] & bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_cor:
        for (i = 0; i < Py_SIZE(aa); i++) {
            c = aa->ob_item[i] | bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_cxor:
        for (i = 0; i < Py_SIZE(aa); i++) {
            c = aa->ob_item[i] ^ bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_subset:
        for (i = 0; i < Py_SIZE(aa); i++)
            if ((aa->ob_item[i] & bb->ob_item[i]) != aa->ob_item[i])
                Py_RETURN_FALSE;
        Py_RETURN_TRUE;
    default:  /* should never happen */
        return NULL;
    }
#undef aa
#undef bb
    return PyLong_FromLongLong(res);
}

#define COUNT_FUNC(oper, ochar)                                         \
static PyObject *                                                       \
count_ ## oper (bitarrayobject *self, PyObject *args)                   \
{                                                                       \
    return two_bitarray_func(args, KERN_c ## oper, "OO:count_" #oper);  \
}                                                                       \
PyDoc_STRVAR(count_ ## oper ## _doc,                                    \
"count_" #oper "(a, b, /) -> int\n\
\n\
Returns `(a " ochar " b).count()`, but is more memory efficient,\n\
as no intermediate bitarray object gets created.")

COUNT_FUNC(and, "&");
COUNT_FUNC(or,  "|");
COUNT_FUNC(xor, "^");


static PyObject *
subset(PyObject *self, PyObject *args)
{
    return two_bitarray_func(args, KERN_subset, "OO:subset");
}

PyDoc_STRVAR(subset_doc,
"subset(a, b, /) -> bool\n\
\n\
Return True if bitarray `a` is a subset of bitarray `b` (False otherwise).\n\
`subset(a, b)` is equivalent to `(a & b).count() == a.count()` but is more\n\
efficient since we can stop as soon as one mismatch is found, and no\n\
intermediate bitarray object gets created.");


static PyMethodDef module_functions[] = {
    {"count_n",   (PyCFunction) count_n,   METH_VARARGS, count_n_doc},
    {"rindex",    (PyCFunction) r_index,   METH_VARARGS, rindex_doc},
    {"count_and", (PyCFunction) count_and, METH_VARARGS, count_and_doc},
    {"count_or",  (PyCFunction) count_or,  METH_VARARGS, count_or_doc},
    {"count_xor", (PyCFunction) count_xor, METH_VARARGS, count_xor_doc},
    {"subset",    (PyCFunction) subset,    METH_VARARGS, subset_doc},
    {NULL,           NULL}  /* sentinel */
};

/*********************** Install Module **************************/

#ifdef IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_utils", 0, -1, module_functions,
};
PyMODINIT_FUNC
PyInit__utils(void)
#else
PyMODINIT_FUNC
init_utils(void)
#endif
{
    PyObject *m;

#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
    if (m == NULL)
        return NULL;
#else
    m = Py_InitModule3("_utils", module_functions, 0);
    if (m == NULL)
        return;
#endif

#ifdef IS_PY3K
    return m;
#endif
}
