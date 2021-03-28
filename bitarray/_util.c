/*
   Copyright (c) 2019 - 2021, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   This file contains the C implementation of some useful utility functions.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "bitarray.h"

#define IS_LE(a)  ((a)->endian == ENDIAN_LITTLE)
#define IS_BE(a)  ((a)->endian == ENDIAN_BIG)

/* set using the Python module function _set_bato() */
static PyObject *bitarray_type_obj = NULL;

/* Return 0 if obj is bitarray.  If not, return -1 and set an exception. */
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
        PyErr_Format(PyExc_TypeError, "bitarray expected, not %s",
                     Py_TYPE(obj)->tp_name);
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
Return the smallest index `i` for which `a[:i].count() == n`.\n\
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
    if (i < 0)
        return PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);

    return PyLong_FromSsize_t(i);
}

PyDoc_STRVAR(rindex_doc,
"rindex(bitarray, value=True, /) -> int\n\
\n\
Return the rightmost index of `bool(value)` in bitarray.\n\
Raises `ValueError` if the value is not present.");

/* --------------------------- unary functions ------------------------- */

static PyObject *
parity(PyObject *module, PyObject *a)
{
    Py_ssize_t i, nbytes;
    unsigned char par = 0;

    if (ensure_bitarray(a) < 0)
        return NULL;

    nbytes = Py_SIZE(a);
#define aa  ((bitarrayobject *) a)
    setunused(aa);
    for (i = 0; i < nbytes; i++)
        par ^= aa->ob_item[i];
#undef aa

    return PyBool_FromLong((long) bitcount_lookup[par] % 2);
}

PyDoc_STRVAR(parity_doc,
"parity(a, /) -> bool\n\
\n\
Return the parity of bitarray `a`.  This is equivalent\n\
to `bool(a.count() % 2)` (but more efficient).");

/* --------------------------- binary functions ------------------------ */

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
Return `(a " ochar " b).count()` in a memory efficient manner,\n\
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

/* ---------------------------- serialization -------------------------- */

static PyObject *
serialize(PyObject *module, PyObject *a)
{
    PyObject *result;
    Py_ssize_t nbytes;
    char *data;

    if (ensure_bitarray(a) < 0)
        return NULL;

    nbytes = Py_SIZE(a);
    data = (char *) PyMem_Malloc(nbytes + 1);
    if (data == NULL)
        return PyErr_NoMemory();

#define aa  ((bitarrayobject *) a)
    *data = (char) (16 * IS_BE(aa) + BITS(nbytes) - aa->nbits);
    setunused(aa);
    memcpy(data + 1, aa->ob_item, (size_t) nbytes);
#undef aa
    result = PyBytes_FromStringAndSize(data, nbytes + 1);
    PyMem_Free((void *) data);
    return result;
}

PyDoc_STRVAR(serialize_doc,
"serialize(bitarray, /) -> bytes\n\
\n\
Return a serialized representation of the bitarray, which may be passed to\n\
`deserialize()`.  It efficiently represents the bitarray object (including\n\
its endianness) and is guaranteed not to change in future releases.");

/* ----------------------------- hexadecimal --------------------------- */

#define HEXDIGITS  "0123456789abcdef"

static int
hex_to_int(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static PyObject *
ba2hex(PyObject *module, PyObject *a)
{
    PyObject *result;
    size_t i, strsize;
    char *str;
    int le, be;

    if (ensure_bitarray(a) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    if (aa->nbits % 4) {
        PyErr_SetString(PyExc_ValueError, "bitarray length not multiple of 4");
        return NULL;
    }

    /* strsize = aa->nbits / 4;  would make strsize odd */
    strsize = 2 * Py_SIZE(a);
    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    le = IS_LE(aa);
    be = IS_BE(aa);
    for (i = 0; i < strsize; i += 2) {
        unsigned char c = aa->ob_item[i / 2];
        str[i + le] = HEXDIGITS[c >> 4];
        str[i + be] = HEXDIGITS[0x0f & c];
    }
    result = Py_BuildValue("s#", str, aa->nbits / 4);
#undef aa
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(ba2hex_doc,
"ba2hex(bitarray, /) -> hexstr\n\
\n\
Return a string containing the hexadecimal representation of\n\
the bitarray (which has to be multiple of 4 in length).");


/* Translate hexadecimal digits into the bitarray's buffer.
   Each digit corresponds to 4 bits in the bitarray.
   The number of digits may be odd. */
static PyObject *
hex2ba(PyObject *module, PyObject *args)
{
    PyObject *a;
    char *str;
    Py_ssize_t i, strsize;
    int le, be;
    static char hex2int[256];
    static int setup = 0;

    if (!setup) {
        for (i = 0; i < 256; i++)
            hex2int[i] = hex_to_int((char) i);
        setup = 1;
    }
    if (!PyArg_ParseTuple(args, "Os#", &a, &str, &strsize))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    if (aa->nbits != 4 * strsize) {
        PyErr_SetString(PyExc_ValueError, "size mismatch");
        return NULL;
    }

    le = IS_LE(aa);
    be = IS_BE(aa);
    assert(le + be == 1 && str[strsize] == 0);
    for (i = 0; i < strsize; i += 2) {
        char x = hex2int[(unsigned char) str[i + le]];
        char y = hex2int[(unsigned char) str[i + be]];
        if (x < 0 || y < 0) {
            /* ignore the terminating NUL - happends when strsize is odd */
            if (i + le == strsize) /* str[i+le] is NUL */
                x = 0;
            if (i + be == strsize) /* str[i+be] is NUL */
                y = 0;
            /* there is an invalid byte - or (non-terminating) NUL */
            if (x < 0 || y < 0) {
                PyErr_SetString(PyExc_ValueError,
                                "Non-hexadecimal digit found");
                return NULL;
            }
        }
        assert(x < 16 && y < 16);
        aa->ob_item[i / 2] = x << 4 | y;
    }
#undef aa
    Py_RETURN_NONE;
}

/* --------------------------------------------------------------------- */

/* RFC 4648 Base32 alphabet */
#define BASE32  "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

/* standard base 64 alphabet */
#define BASE64  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" \
    "0123456789+/"

static int
digit_to_int(char c, int n)
{
    if (n <= 16) {              /* base 2, 4, 8, 16 */
        int i = hex_to_int(c);
        if (0 <= i && i < n)
            return i;
    }
    if (n == 32) {              /* base 32 */
        if ('A' <= c && c <= 'Z')
            return c - 'A';
        if ('2' <= c && c <= '7')
            return c - '2' + 26;
    }
    if (n == 64) {              /* base 64 */
        if ('A' <= c && c <= 'Z')
            return c - 'A';
        if ('a' <= c && c <= 'z')
            return c - 'a' + 26;
        if ('0' <= c && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
    }
    return -1;
}

static int
base_to_length(int n)
{
    int k;

    for (k = 1; k < 7; k++) {
        if (n == (1 << k))
            return k;
    }
    PyErr_SetString(PyExc_ValueError, "base must be 2, 4, 8, 16, 32 or 64");
    return -1;
}

static PyObject *
ba2base(PyObject *module, PyObject *args)
{
    PyObject *result, *a;
    size_t i, strsize;
    char *str, *digits;
    int n, m, d, k, le;

    if (!PyArg_ParseTuple(args, "iO:ba2ascii", &n, &a))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;
    m = base_to_length(n);
    if (m < 0)
        return NULL;

    if (n <= 16)
        digits = HEXDIGITS;
    else if (n == 32)
        digits = BASE32;
    else if (n == 64)
        digits = BASE64;

#define aa  ((bitarrayobject *) a)
    if (aa->nbits % m)
        return PyErr_Format(PyExc_ValueError,
                            "bitarray length not multiple of %d", m);

    strsize = aa->nbits / m;
    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    le = IS_LE(aa);
    for (i = 0; i < strsize; i++) {
        d = 0;
        for (k = 0; k < m; k++)
            d |= GETBIT(aa, m * i + (le ? k : (m - k - 1))) << k;
        str[i] = digits[d];
    }
    result = Py_BuildValue("s#", str, strsize);
#undef aa
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(ba2base_doc,
"ba2base(n, bitarray, /) -> str\n\
\n\
Return a string containing the base `n` ascii representation of\n\
the bitarray.  Allowed values for `n` are 2, 4, 8, 16, 32 and 64.\n\
The bitarray has to be multiple of length 1, 2, 3, 4, 5 or 6 respectively.");


static PyObject *
base2ba(PyObject *module, PyObject *args)
{
    PyObject *a;
    Py_ssize_t i, strsize;
    char *str;
    int n, m, d, k, le;

    if (!PyArg_ParseTuple(args, "iOs#", &n, &a, &str, &strsize))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;
    m = base_to_length(n);
    if (m < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    if (aa->nbits != m * strsize) {
        PyErr_SetString(PyExc_ValueError, "size mismatch");
        return NULL;
    }
    memset(aa->ob_item, 0x00, (size_t) Py_SIZE(a));

    le = IS_LE(aa);
    for (i = 0; i < strsize; i++) {
        d = digit_to_int(str[i], n);
        if (d < 0) {
            PyErr_SetString(PyExc_ValueError, "Invalid digit found");
            return NULL;
        }
        for (k = 0; k < m; k++)
            setbit(aa, m * i + (le ? k : (m - k - 1)), d & (1 << k));
    }
#undef aa
    Py_RETURN_NONE;
}

/* --------------------------------------------------------------------- */

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
    {"parity",    (PyCFunction) parity,    METH_O,       parity_doc},
    {"count_and", (PyCFunction) count_and, METH_VARARGS, count_and_doc},
    {"count_or",  (PyCFunction) count_or,  METH_VARARGS, count_or_doc},
    {"count_xor", (PyCFunction) count_xor, METH_VARARGS, count_xor_doc},
    {"subset",    (PyCFunction) subset,    METH_VARARGS, subset_doc},
    {"serialize", (PyCFunction) serialize, METH_O,       serialize_doc},
    {"ba2hex",    (PyCFunction) ba2hex,    METH_O,       ba2hex_doc},
    {"_hex2ba",   (PyCFunction) hex2ba,    METH_VARARGS, 0},
    {"ba2base",   (PyCFunction) ba2base,   METH_VARARGS, ba2base_doc},
    {"_base2ba",  (PyCFunction) base2ba,   METH_VARARGS, 0},
    {"_set_bato", (PyCFunction) set_bato,  METH_O,       0},
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
    return m;
#else
    m = Py_InitModule3("_util", module_functions, 0);
    if (m == NULL)
        return;
#endif
}
