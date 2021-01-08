/*
   Copyright (c) 2019 - 2021, Ilan Schnell
   bitarray is published under the PSF license.

   This file contains the C implementation of some useful utility functions.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "bitarray.h"

/* set using the Python module function _set_bato() */
static PyObject *bitarray_type_obj = NULL;

/* Return 0 if obj is bitarray.  If not, returns -1 and sets an exception. */
static int
ensure_bitarray(PyObject *obj)
{
    int t;

    if (bitarray_type_obj == NULL)
        Py_FatalError("bitarray_type_obj missing");
    t = PyObject_IsInstance(obj, bitarray_type_obj);
    if (t < 0)
        return -1;
    if (t == 0) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected");
        return -1;
    }
    return 0;
}

/************* start of actual functionality in this module ***************/

/* return the smallest index i for which a.count(1, 0, i) == n, or when
   n exceeds the total count return -1  */
static Py_ssize_t
count_to_n(bitarrayobject *a, Py_ssize_t n)
{
    Py_ssize_t i = 0;        /* index */
    Py_ssize_t j = 0;        /* total count up to index */
    Py_ssize_t block_start, block_stop, k, m;
    unsigned char c;

    if (n == 0)
        return 0;

#define BLOCK_BITS  8192
    /* by counting big blocks we save comparisons */
    while (i + BLOCK_BITS < a->nbits) {
        m = 0;
        assert(i % 8 == 0);
        block_start = i / 8;
        block_stop = block_start + (BLOCK_BITS / 8);
        for (k = block_start; k < block_stop; k++) {
            assert(k < Py_SIZE(a));
            c = a->ob_item[k];
            m += bitcount_lookup[c];
        }
        if (j + m >= n)
            break;
        j += m;
        i += BLOCK_BITS;
    }
#undef BLOCK_BITS

    while (i + 8 < a->nbits) {
        k = i / 8;
        assert(k < Py_SIZE(a));
        c = a->ob_item[k];
        m = bitcount_lookup[c];
        if (j + m >= n)
            break;
        j += m;
        i += 8;
    }

    while (j < n && i < a->nbits ) {
        j += GETBIT(a, i);
        i++;
    }
    if (j < n)
        return -1;

    return i;
}

/* return index of last occurrence of vi, -1 when x is not in found. */
static Py_ssize_t
find_last(bitarrayobject *a, int vi)
{
    Py_ssize_t i, j;
    char c;

    if (a->nbits == 0)
        return -1;

    /* search within top byte */
    for (i = a->nbits - 1; i >= BITS(a->nbits / 8); i--)
        if (GETBIT(a, i) == vi)
            return i;

    if (i < 0)  /* not found within top byte */
        return -1;
    assert((i + 1) % 8 == 0);

    /* seraching for 1 means: break when byte is not 0x00
       searching for 0 means: break when byte is not 0xff */
    c = vi ? 0x00 : 0xff;

    /* skip ahead by checking whole bytes */
    for (j = BYTES(i) - 1; j >= 0; j--)
        if (c ^ a->ob_item[j])
            break;

    if (j < 0)  /* not found within bytes */
        return -1;

    /* search within byte found */
    for (i = BITS(j + 1) - 1; i >= BITS(j); i--)
        if (GETBIT(a, i) == vi)
            return i;

    return -1;
}

/* translation table which swaps the 4 highest with the 4 lowest bits in
   each byte - to be used as argument of bytes.translate() */
static PyObject *
make_swap_hilo_bytes(void)
{
    char bytes[256];
    int i;

    for (i = 0; i < 256; i++)
        bytes[i] = (char) (((i & 0x0f) << 4) ^ (i >> 4));

    return PyBytes_FromStringAndSize(bytes, 256);
}

/****************************** Module functions **************************/

static PyObject *
count_n(PyObject *module, PyObject *args)
{
    PyObject *a;
    Py_ssize_t n, i;

    if (!PyArg_ParseTuple(args, "On:count_n", &a, &n))
        return NULL;

    if (ensure_bitarray(a) < 0)
        return NULL;

    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "non-negative integer expected");
        return NULL;
    }
#define aa  ((bitarrayobject *) a)
    if (n > aa->nbits)  {
        PyErr_SetString(PyExc_ValueError, "n larger than bitarray size");
        return NULL;
    }
    i = count_to_n(aa, n);        /* do actual work here */
#undef aa
    if (i < 0) {
        PyErr_SetString(PyExc_ValueError, "n exceeds total count");
        return NULL;
    }
    return PyLong_FromSsize_t(i);
}

PyDoc_STRVAR(count_n_doc,
"count_n(a, n, /) -> int\n\
\n\
Find the smallest index `i` for which `a[:i].count() == n`.\n\
Raises `ValueError`, when n exceeds total count (`a.count()`).");


static PyObject *
r_index(PyObject *module, PyObject *args)
{
    PyObject *x = Py_True, *a;
    Py_ssize_t i;
    int vi;

    if (!PyArg_ParseTuple(args, "O|O:rindex", &a, &x))
        return NULL;

    if (ensure_bitarray(a) < 0)
        return NULL;

    vi = PyObject_IsTrue(x);
    if (vi < 0)
        return NULL;

    i = find_last((bitarrayobject *) a, vi);
    if (i < 0) {
        PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);
        return NULL;
    }
    return PyLong_FromSsize_t(i);
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
    Py_ssize_t res = 0, nbytes, i;
    PyObject *a, *b;
    unsigned char c;

    if (!PyArg_ParseTuple(args, format, &a, &b))
        return NULL;
    if (ensure_bitarray(a) < 0 || ensure_bitarray(b) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
#define bb  ((bitarrayobject *) b)
    if (aa->nbits != bb->nbits || aa->endian != bb->endian) {
        PyErr_SetString(PyExc_ValueError,
                        "bitarrays of equal length and endianness expected");
        return NULL;
    }
    setunused(aa);
    setunused(bb);
    assert(Py_SIZE(a) == Py_SIZE(b));
    nbytes = Py_SIZE(a);

    switch (kern) {
    case KERN_cand:
        for (i = 0; i < nbytes; i++) {
            c = aa->ob_item[i] & bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_cor:
        for (i = 0; i < nbytes; i++) {
            c = aa->ob_item[i] | bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_cxor:
        for (i = 0; i < nbytes; i++) {
            c = aa->ob_item[i] ^ bb->ob_item[i];
            res += bitcount_lookup[c];
        }
        break;
    case KERN_subset:
        for (i = 0; i < nbytes; i++) {
            if ((aa->ob_item[i] & bb->ob_item[i]) != aa->ob_item[i])
                Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    default:  /* cannot happen */
        return NULL;
    }
#undef aa
#undef bb
    return PyLong_FromSsize_t(res);
}

#define COUNT_FUNC(oper, ochar)                                         \
static PyObject *                                                       \
count_ ## oper (bitarrayobject *module, PyObject *args)                 \
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
subset(PyObject *module, PyObject *args)
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


/* set bitarray_type_obj (bato) */
static PyObject *
set_bato(PyObject *module, PyObject *obj)
{
    bitarray_type_obj = obj;
    Py_RETURN_NONE;
}

static PyMethodDef module_functions[] = {
    {"count_n",   (PyCFunction) count_n,   METH_VARARGS, count_n_doc},
    {"rindex",    (PyCFunction) r_index,   METH_VARARGS, rindex_doc},
    {"count_and", (PyCFunction) count_and, METH_VARARGS, count_and_doc},
    {"count_or",  (PyCFunction) count_or,  METH_VARARGS, count_or_doc},
    {"count_xor", (PyCFunction) count_xor, METH_VARARGS, count_xor_doc},
    {"subset",    (PyCFunction) subset,    METH_VARARGS, subset_doc},
    {"_set_bato", (PyCFunction) set_bato,  METH_O,       },
    {NULL,        NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

#ifdef IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_util", 0, -1, module_functions,
};
#endif

PyMODINIT_FUNC
#ifdef IS_PY3K
PyInit__util(void)
#else
init_util(void)
#endif
{
    PyObject *m;

#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
    if (m == NULL)
        return NULL;
#else
    m = Py_InitModule3("_util", module_functions, 0);
    if (m == NULL)
        return;
#endif

    PyModule_AddObject(m, "_swap_hilo_bytes", make_swap_hilo_bytes());
#ifdef IS_PY3K
    return m;
#endif
}
