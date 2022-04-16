/*
   Copyright (c) 2019 - 2022, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   This file contains the C implementation of some useful utility functions.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "bitarray.h"

/* set using the Python module function _set_bato() */
static PyObject *bitarray_type_obj = NULL;

/* Return 0 if obj is bitarray.  If not, return -1 and set an exception. */
static int
ensure_bitarray(PyObject *obj)
{
    int t;

    if (bitarray_type_obj == NULL)
        Py_FatalError("bitarray_type_obj not set");
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

/* ensure object is a bitarray of given length */
static int
ensure_ba_of_length(PyObject *a, const Py_ssize_t n)
{
    if (ensure_bitarray(a) < 0)
        return -1;
    if (((bitarrayobject *) a)->nbits != n) {
        PyErr_SetString(PyExc_ValueError, "size mismatch");
        return -1;
    }
    return 0;
}

/* ------------------------------- count_n ----------------------------- */

/* return the smallest index i for which a.count(vi, 0, i) == n, or when
   n exceeds the total count return -1  */
static Py_ssize_t
count_to_n(bitarrayobject *a, Py_ssize_t n, int vi)
{
    const Py_ssize_t nbits = a->nbits;
    unsigned char *ucbuff = (unsigned char *) a->ob_item;
    Py_ssize_t i = 0;        /* index */
    Py_ssize_t j = 0;        /* total count up to index */
    Py_ssize_t block_start, block_stop, k, m;

    assert(0 <= n && n <= nbits);
    if (n == 0)
        return 0;

#define BLOCK_BITS  8192
    /* by counting big blocks we save comparisons */
    while (i + BLOCK_BITS < nbits) {
        m = 0;
        assert(i % 8 == 0);
        block_start = i >> 3;
        block_stop = block_start + (BLOCK_BITS >> 3);
        assert(block_stop <= Py_SIZE(a));
        for (k = block_start; k < block_stop; k++)
            m += bitcount_lookup[ucbuff[k]];
        if (!vi)
            m = BLOCK_BITS - m;
        if (j + m >= n)
            break;
        j += m;
        i += BLOCK_BITS;
    }
#undef BLOCK_BITS

    while (i + 8 < nbits) {
        k = i >> 3;
        assert(k < Py_SIZE(a));
        m = bitcount_lookup[ucbuff[k]];
        if (!vi)
            m = 8 - m;
        if (j + m >= n)
            break;
        j += m;
        i += 8;
    }

    while (j < n && i < nbits) {
        j += vi ? getbit(a, i) : 1 - getbit(a, i);
        i++;
    }
    if (j < n)  /* n exceeds total count */
        return -1;

    return i;
}

static PyObject *
count_n(PyObject *module, PyObject *args)
{
    PyObject *value = Py_True, *a;
    Py_ssize_t n, i;
    int vi;

    if (!PyArg_ParseTuple(args, "On|O:count_n", &a, &n, &value))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "non-negative integer expected");
        return NULL;
    }
    if ((vi = pybit_as_int(value)) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    if (n > aa->nbits)  {
        PyErr_SetString(PyExc_ValueError, "n larger than bitarray size");
        return NULL;
    }
    i = count_to_n(aa, n, vi);        /* do actual work here */
#undef aa
    if (i < 0) {
        PyErr_SetString(PyExc_ValueError, "n exceeds total count");
        return NULL;
    }
    return PyLong_FromSsize_t(i);
}

PyDoc_STRVAR(count_n_doc,
"count_n(a, n, value=1, /) -> int\n\
\n\
Return lowest index `i` for which `a[:i].count(value) == n`.\n\
Raises `ValueError`, when n exceeds total count (`a.count(value)`).");

/* ----------------------------- right index --------------------------- */

/* return index of highest occurrence of vi in self[a:b], -1 when not found */
static Py_ssize_t
find_last(bitarrayobject *self, int vi, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t res, i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(0 <= vi && vi <= 1);
    if (n <= 0)
        return -1;

    /* the logic here is the same as in find_bit() in _bitarray.c */
#ifdef PY_UINT64_T
    if (n > 64) {
        Py_ssize_t word_a = (a + 63) / 64;
        Py_ssize_t word_b = b / 64;
        PY_UINT64_T *wbuff = (PY_UINT64_T *) self->ob_item, w = vi ? 0 : ~0;

        if ((res = find_last(self, vi, 64 * word_b, b)) >= 0)
            return res;

        for (i = word_b - 1; i >= word_a; i--) {  /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_last(self, vi, 64 * i, 64 * i + 64);
        }
        return find_last(self, vi, a, 64 * word_a);
    }
#endif
    if (n > 8) {
        Py_ssize_t byte_a = BYTES(a);
        Py_ssize_t byte_b = b / 8;
        char *buff = self->ob_item, c = vi ? 0 : ~0;

        if ((res = find_last(self, vi, 8 * byte_b, b)) >= 0)
            return res;

        for (i = byte_b - 1; i >= byte_a; i--) {  /* skip bytes */
            assert_byte_in_range(self, i);
            if (c ^ buff[i])
                return find_last(self, vi, 8 * i, 8 * i + 8);
        }
        return find_last(self, vi, a, 8 * byte_a);
    }
    assert(n <= 8);
    for (i = b - 1; i >= a; i--) {
        if (getbit(self, i) == vi)
            return i;
    }
    return -1;
}

static PyObject *
r_index(PyObject *module, PyObject *args)
{
    PyObject *value = Py_True, *a;
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, res;
    int vi;

    if (!PyArg_ParseTuple(args, "O|Onn:rindex", &a, &value, &start, &stop))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;
    if ((vi = pybit_as_int(value)) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    adjust_indices(aa->nbits, &start, &stop, 1);
    res = find_last(aa, vi, start, stop);
#undef aa
    if (res < 0)
        return PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);

    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(rindex_doc,
"rindex(bitarray, value=1, start=0, stop=<end of array>, /) -> int\n\
\n\
Return the rightmost (highest) index of `value` in bitarray.\n\
Raises `ValueError` if the value is not present.");

/* --------------------------- unary functions ------------------------- */

static PyObject *
parity(PyObject *module, PyObject *a)
{
    unsigned char par = 0;
    Py_ssize_t i;

    if (ensure_bitarray(a) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    for (i = 0; i < aa->nbits / 8; i++)
        par ^= aa->ob_item[i];
    if (aa->nbits % 8)
        par ^= zeroed_last_byte(aa);
#undef aa

    return PyLong_FromLong((long) bitcount_lookup[par] % 2);
}

PyDoc_STRVAR(parity_doc,
"parity(a, /) -> int\n\
\n\
Return the parity of bitarray `a`.\n\
This is equivalent to `a.count() % 2` (but more efficient).");

/* --------------------------- binary functions ------------------------ */

enum kernel_type {
    KERN_cand,     /* count bitwise and -> int */
    KERN_cor,      /* count bitwise or -> int */
    KERN_cxor,     /* count bitwise xor -> int */
    KERN_subset,   /* is subset -> bool */
};

static PyObject *
binary_function(PyObject *args, enum kernel_type kern, const char *format)
{
    Py_ssize_t res = 0, s, i;
    PyObject *a, *b;
    char *buff_a, *buff_b;
    unsigned char c;
    int r;

    if (!PyArg_ParseTuple(args, format, &a, &b))
        return NULL;
    if (ensure_bitarray(a) < 0 || ensure_bitarray(b) < 0)
        return NULL;

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
    buff_a = aa->ob_item;
    buff_b = bb->ob_item;
    s = aa->nbits / 8;       /* number of whole bytes in buffer */
    r = aa->nbits % 8;       /* remaining bits  */

    switch (kern) {
    case KERN_cand:
        for (i = 0; i < s; i++) {
            c = buff_a[i] & buff_b[i];
            res += bitcount_lookup[c];
        }
        if (r) {
            c = zeroed_last_byte(aa) & zeroed_last_byte(bb);
            res += bitcount_lookup[c];
        }
        break;

    case KERN_cor:
        for (i = 0; i < s; i++) {
            c = buff_a[i] | buff_b[i];
            res += bitcount_lookup[c];
        }
        if (r) {
            c = zeroed_last_byte(aa) | zeroed_last_byte(bb);
            res += bitcount_lookup[c];
        }
        break;

    case KERN_cxor:
        for (i = 0; i < s; i++) {
            c = buff_a[i] ^ buff_b[i];
            res += bitcount_lookup[c];
        }
        if (r) {
            c = zeroed_last_byte(aa) ^ zeroed_last_byte(bb);
            res += bitcount_lookup[c];
        }
        break;

    case KERN_subset:
        for (i = 0; i < s; i++) {
            if ((buff_a[i] & buff_b[i]) != buff_a[i])
                Py_RETURN_FALSE;
        }
        if (r) {
            if ((zeroed_last_byte(aa) & zeroed_last_byte(bb)) !=
                 zeroed_last_byte(aa))
                Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;

    default:
        Py_UNREACHABLE();
    }
#undef aa
#undef bb
    return PyLong_FromSsize_t(res);
}

#define COUNT_FUNC(oper, ochar)                                         \
static PyObject *                                                       \
count_ ## oper (PyObject *module, PyObject *args)                       \
{                                                                       \
    return binary_function(args, KERN_c ## oper, "OO:count_" #oper);    \
}                                                                       \
PyDoc_STRVAR(count_ ## oper ## _doc,                                    \
"count_" #oper "(a, b, /) -> int\n\
\n\
Return `(a " ochar " b).count()` in a memory efficient manner,\n\
as no intermediate bitarray object gets created.")

COUNT_FUNC(and, "&");           /* count_and */
COUNT_FUNC(or,  "|");           /* count_or  */
COUNT_FUNC(xor, "^");           /* count_xor */


static PyObject *
subset(PyObject *module, PyObject *args)
{
    return binary_function(args, KERN_subset, "OO:subset");
}

PyDoc_STRVAR(subset_doc,
"subset(a, b, /) -> bool\n\
\n\
Return `True` if bitarray `a` is a subset of bitarray `b`.\n\
`subset(a, b)` is equivalent to `(a & b).count() == a.count()` but is more\n\
efficient since we can stop as soon as one mismatch is found, and no\n\
intermediate bitarray object gets created.");

/* ---------------------------- serialization -------------------------- */

static PyObject *
serialize(PyObject *module, PyObject *a)
{
    PyObject *result;
    Py_ssize_t nbytes;
    char *str;

    if (ensure_bitarray(a) < 0)
        return NULL;

    nbytes = Py_SIZE(a);
    result = PyBytes_FromStringAndSize(NULL, nbytes + 1);
    if (result == NULL)
        return NULL;

    str = PyBytes_AsString(result);
#define aa  ((bitarrayobject *) a)
    *str = (char) (16 * IS_BE(aa) + setunused(aa));
    memcpy(str + 1, aa->ob_item, (size_t) nbytes);
#undef aa
    return result;
}

PyDoc_STRVAR(serialize_doc,
"serialize(bitarray, /) -> bytes\n\
\n\
Return a serialized representation of the bitarray, which may be passed to\n\
`deserialize()`.  It efficiently represents the bitarray object (including\n\
its endianness) and is guaranteed not to change in future releases.");

/* ----------------------------- hexadecimal --------------------------- */

static const char hexdigits[] = "0123456789abcdef";

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

    /* strsize = aa->nbits / 4;  could make strsize odd */
    strsize = 2 * Py_SIZE(a);
    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    le = IS_LE(aa);
    be = IS_BE(aa);
    for (i = 0; i < strsize; i += 2) {
        unsigned char c = aa->ob_item[i / 2];

        str[i + le] = hexdigits[c >> 4];
        str[i + be] = hexdigits[0x0f & c];
    }
    assert((size_t) aa->nbits / 4 <= strsize);
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

    if (!PyArg_ParseTuple(args, "Os#", &a, &str, &strsize))
        return NULL;
    if (ensure_ba_of_length(a, 4 * strsize) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    le = IS_LE(aa);
    be = IS_BE(aa);
    assert(le + be == 1 && str[strsize] == 0);
    for (i = 0; i < strsize; i += 2) {
        int x = hex_to_int(str[i + le]);
        int y = hex_to_int(str[i + be]);

        if (x < 0 || y < 0) {
            /* ignore the terminating NUL - happends when strsize is odd */
            if (i + le == strsize) /* str[i+le] is NUL */
                x = 0;
            if (i + be == strsize) /* str[i+be] is NUL */
                y = 0;
            /* there is an invalid byte - or (non-terminating) NUL */
            if (x < 0 || y < 0) {
                PyErr_SetString(PyExc_ValueError,
                                "non-hexadecimal digit found");
                return NULL;
            }
        }
        assert(0 <= x && x < 16 && 0 <= y && y < 16);
        aa->ob_item[i / 2] = x << 4 | y;
    }
#undef aa
    Py_RETURN_NONE;
}

/* ----------------------- base 2, 4, 8, 16, 32, 64 -------------------- */

/* RFC 4648 Base32 alphabet */
static const char base32_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/* standard base 64 alphabet */
static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int
digit_to_int(int n, char c)
{
    int i;

    switch (n) {
    case 32:                    /* base 32 */
        if ('A' <= c && c <= 'Z')
            return c - 'A';
        if ('2' <= c && c <= '7')
            return c - '2' + 26;
        break;

    case 64:                    /* base 64 */
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
        break;

    default:                    /* base 2, 4, 8, 16 */
        i = hex_to_int(c);
        if (i < n)
            return i;
    }
    return -1;
}

/* return m = log2(n) for m = 1..6 */
static int
base_to_length(int n)
{
    int m;

    for (m = 1; m < 7; m++) {
        if (n == (1 << m))
            return m;
    }
    PyErr_Format(PyExc_ValueError,
                 "base must be 2, 4, 8, 16, 32 or 64, not %d", n);
    return -1;
}

static PyObject *
ba2base(PyObject *module, PyObject *args)
{
    const char *alphabet;
    PyObject *result, *a;
    size_t i, strsize;
    char *str;
    int n, m, le;

    if (!PyArg_ParseTuple(args, "iO:ba2base", &n, &a))
        return NULL;
    if ((m = base_to_length(n)) < 0)
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;

    switch (n) {
    case 32: alphabet = base32_alphabet; break;
    case 64: alphabet = base64_alphabet; break;
    default: alphabet = hexdigits;
    }

#define aa  ((bitarrayobject *) a)
    if (aa->nbits % m)
        return PyErr_Format(PyExc_ValueError,
                            "bitarray length must be multiple of %d", m);

    strsize = aa->nbits / m;
    if ((str = (char *) PyMem_Malloc(strsize)) == NULL)
        return PyErr_NoMemory();

    le = IS_LE(aa);
    for (i = 0; i < strsize; i++) {
        int j, x = 0;

        for (j = 0; j < m; j++) {
            const int k = le ? j : (m - j - 1);
            x |= getbit(aa, i * m + k) << j;
        }
        str[i] = alphabet[x];
    }
    result = Py_BuildValue("s#", str, strsize);
#undef aa
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(ba2base_doc,
"ba2base(n, bitarray, /) -> str\n\
\n\
Return a string containing the base `n` ASCII representation of\n\
the bitarray.  Allowed values for `n` are 2, 4, 8, 16, 32 and 64.\n\
The bitarray has to be multiple of length 1, 2, 3, 4, 5 or 6 respectively.\n\
For `n=16` (hexadecimal), `ba2hex()` will be much faster, as `ba2base()`\n\
does not take advantage of byte level operations.\n\
For `n=32` the RFC 4648 Base32 alphabet is used, and for `n=64` the\n\
standard base 64 alphabet is used.");


/* Translate ASCII digits into the bitarray's buffer.
   The (Python) arguments to this functions are:
   - base n, one of 2, 4, 8, 16, 32, 64  (n=2^m   where m bits per digit)
   - bitarray (of length m * len(s)) whose buffer is written into
   - byte object s containing the ASCII digits
*/
static PyObject *
base2ba(PyObject *module, PyObject *args)
{
    PyObject *a = NULL;
    Py_ssize_t i, strsize = 0;
    char *str = NULL;
    int n, m, le;

    if (!PyArg_ParseTuple(args, "i|Os#", &n, &a, &str, &strsize))
        return NULL;
    if ((m = base_to_length(n)) < 0)
        return NULL;
    if (a == NULL)
        return PyLong_FromLong(m);
    if (ensure_ba_of_length(a, m * strsize) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    le = IS_LE(aa);
    for (i = 0; i < strsize; i++) {
        int j, d = digit_to_int(n, str[i]);

        if (d < 0) {
            unsigned char c = str[i];
            return PyErr_Format(PyExc_ValueError, "invalid digit found for "
                                "base %d, got '%c' (0x%02x)", n, c, c);
        }
        for (j = 0; j < m; j++) {
            const int k = le ? j : (m - j - 1);
            setbit(aa, i * m + k, d & (1 << j));
        }
    }
#undef aa
    Py_RETURN_NONE;
}

/* ------------------- variable length bitarray format ----------------- */

/* grow buffer by at least one byte */
static int
grow_buffer(bitarrayobject *self)
{
    size_t newsize = Py_SIZE(self) + 1;

    assert_nbits(self);
    assert(self->allocated >= Py_SIZE(self));
    assert(self->ob_exports == 0);
    assert(self->buffer == NULL);
    assert(self->readonly == 0);

    /* standard growth pattern */
    newsize = (newsize + (newsize >> 4) + (newsize < 8 ? 3 : 7)) & ~(size_t) 3;

    self->ob_item = PyMem_Realloc(self->ob_item, newsize);
    if (self->ob_item == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    Py_SET_SIZE(self, newsize);
    self->allocated = newsize;
    self->nbits = 8 * newsize;
    return 0;
}

/* PADBITS is always 3 - the number of bits that represent the number of
   padding bits.  The actual number of padding bits is called 'padding'
   below, and is in range(0, 7).
   Also note that 'padding' refers to the pad bits within the variable
   length format, which is not the same as the pad bits of the actual
   bitarray.  For example, b'\x10' has padding = 1, and decodes to
   bitarray('000'), which has 5 pad bits. */
#define PADBITS  3

/* consume iterator while decoding bytes into bitarray */
static PyObject *
vl_decode(PyObject *module, PyObject *args)
{
    PyObject *iter, *item, *a;
    Py_ssize_t padding = 0;  /* number of pad bits read from header byte */
    Py_ssize_t i = 0;        /* bit counter */
    unsigned char b = 0x80;  /* empty stream will raise StopIteration */
    Py_ssize_t k;

    if (!PyArg_ParseTuple(args, "OO", &iter, &a))
        return NULL;
    if (!PyIter_Check(iter))
        return PyErr_Format(PyExc_TypeError, "iterator or bytes expected, "
                            "got '%s'", Py_TYPE(iter)->tp_name);

#define aa  ((bitarrayobject *) a)
    while ((item = PyIter_Next(iter))) {
#ifdef IS_PY3K
        if (PyLong_Check(item))
            b = (unsigned char) PyLong_AsLong(item);
#else
        if (PyBytes_Check(item))
            b = (unsigned char) *PyBytes_AS_STRING(item);
#endif
        else {
            PyErr_Format(PyExc_TypeError, "int (byte) iterator expected, "
                         "got '%s' element", Py_TYPE(item)->tp_name);
            Py_DECREF(item);
            return NULL;
        }
        Py_DECREF(item);

        if (i + 6 >= aa->nbits && grow_buffer(aa) < 0)
            return NULL;
        assert(i + 6 < aa->nbits);

        if (i == 0) {
            padding = (b & 0x70) >> 4;
            if (padding >= 7 || ((b & 0x80) == 0 && padding > 4))
                return PyErr_Format(PyExc_ValueError,
                                    "invalid header byte: 0x%02x", b);
            for (k = 0; k < 4; k++)
                setbit(aa, i++, (0x08 >> k) & b);
        }
        else {
            for (k = 0; k < 7; k++)
                setbit(aa, i++, (0x40 >> k) & b);
        }
        if ((b & 0x80) == 0)
            break;
    }
    /* set final length of bitarray */
    aa->nbits = i - padding;
    Py_SET_SIZE(a, BYTES(aa->nbits));
    assert_nbits(aa);
#undef aa

    if (PyErr_Occurred())       /* from PyIter_Next() */
        return NULL;

    if (b & 0x80)
        return PyErr_Format(PyExc_StopIteration, "no terminating byte found, "
                            "bytes read: %zd", (i + PADBITS) / 7);

    Py_RETURN_NONE;
}

static PyObject *
vl_encode(PyObject *module, PyObject *a)
{
    PyObject *result;
    Py_ssize_t padding, n, m, i;
    Py_ssize_t j = 0;           /* byte conter */
    char *str;

    if (ensure_bitarray(a) < 0)
        return NULL;

#define aa  ((bitarrayobject *) a)
    n = (aa->nbits + PADBITS + 6) / 7;  /* number of resulting bytes */
    m = 7 * n - PADBITS;      /* number of bits resulting bytes can hold */
    padding = m - aa->nbits;  /* number of pad bits */
    assert(0 <= padding && padding < 7);

    result = PyBytes_FromStringAndSize(NULL, n);
    if (result == NULL)
        return NULL;

    str = PyBytes_AsString(result);
    str[0] = aa->nbits > 4 ? 0x80 : 0x00;  /* leading bit */
    str[0] |= padding << 4;                /* encode padding */
    for (i = 0; i < 4 && i < aa->nbits; i++)
        str[0] |= (0x08 >> i) * getbit(aa, i);

    for (i = 4; i < aa->nbits; i++) {
        const int k = (i - 4) % 7;

        if (k == 0) {
            j++;
            str[j] = j < n - 1 ? 0x80 : 0x00;  /* leading bit */
        }
        str[j] |= (0x40 >> k) * getbit(aa, i);
    }
#undef aa
    assert(j == n - 1);

    return result;
}

PyDoc_STRVAR(vl_encode_doc,
"vl_encode(bitarray, /) -> bytes\n\
\n\
Return variable length binary representation of bitarray.\n\
This representation is useful for efficiently storing small bitarray\n\
in a binary stream.  Use `vl_decode()` for decoding.");

/* ----------------------- canonical Huffman decoder ------------------- */

#define MAXBITS  31                  /* maximum bits in a code */

typedef struct {
    PyObject_HEAD
    bitarrayobject *array;           /* bitarray we're decoding */
    Py_ssize_t index;                /* current index in bitarray */
    Py_ssize_t count[MAXBITS + 1];   /* array of bit length counts */
    PyObject *symbols;               /* list of symbols */
} chdi_obj;                          /* canonical Huffman decode iterator */

static PyTypeObject CHDI_Type;

/* set elements in count (from list) and return the sum (-1 on error) */
static Py_ssize_t
set_count(Py_ssize_t *count, PyObject *list)
{
    Py_ssize_t i, c, res = 0;

    if (PyList_GET_SIZE(list) > MAXBITS) {
        PyErr_Format(PyExc_ValueError, "counts list cannot have more than %d "
                     "elements", MAXBITS);
        return -1;
    }

    for (i = 1; i <= MAXBITS; i++) {
        c = 0;
        if (i < PyList_GET_SIZE(list)) {
            PyObject *item = PyList_GET_ITEM(list, i);
            c = PyNumber_AsSsize_t(item, NULL);
            if (c == -1 && PyErr_Occurred())
                return -1;
            if (c < 0) {
                PyErr_SetString(PyExc_ValueError, "count cannot be negative");
                return -1;
            }
        }
        count[i] = c;
        res += c;
    }
    return res;
}

/* create a new initialized canonical Huffman decode iterator object */
static PyObject *
chdi_new(PyObject *module, PyObject *args)
{
    PyObject *a, *counts, *symbols;
    Py_ssize_t counts_sum;
    chdi_obj *it;       /* iterator to be returned */

    if (!PyArg_ParseTuple(args, "OOO:count_n", &a, &counts, &symbols))
        return NULL;
    if (ensure_bitarray(a) < 0)
        return NULL;
    if (!PyList_Check(counts))
        return PyErr_Format(PyExc_TypeError, "list expected for counts, "
                            "got %s", Py_TYPE(counts)->tp_name);
    if (!PyList_Check(symbols))
        return PyErr_Format(PyExc_TypeError, "list expected for symbols, "
                            "got %s", Py_TYPE(symbols)->tp_name);

    it = PyObject_GC_New(chdi_obj, &CHDI_Type);
    if (it == NULL)
        return NULL;

    if ((counts_sum = set_count(it->count, counts)) < 0)
        goto error;

    if (counts_sum != PyList_GET_SIZE(symbols)) {
        PyErr_Format(PyExc_ValueError, "sum(counts) = %zd, but len(symbols) "
                     "= %zd", counts_sum, PyList_GET_SIZE(symbols));
        goto error;
    }
    Py_INCREF(a);
    it->array = (bitarrayobject *) a;
    it->index = 0;
    Py_INCREF(symbols);
    it->symbols = symbols;

    PyObject_GC_Track(it);
    return (PyObject *) it;

 error:
    PyObject_GC_Del(it);
    return NULL;
}

PyDoc_STRVAR(chdi_doc,
"canonical_decode(bitarray, counts, symbols, /) -> iterator\n\
\n\
Decode bitarray which was encoded using a canonical Huffman code\n\
with `counts` (a list of the number of bit counts for each code length)\n\
and `symbols` (a list of symbols).  ...");

/* This function is based on the function decode() in:
   https://github.com/madler/zlib/blob/master/contrib/puff/puff.c */
static PyObject *
chdi_next(chdi_obj *it)
{
    PyObject *symbol;  /* symbol we return (if any) */
    Py_ssize_t len;    /* current number of bits in code */
    Py_ssize_t code;   /* len bits being decoded */
    Py_ssize_t first;  /* first code of length len */
    Py_ssize_t count;  /* number of codes of length len */
    Py_ssize_t index;  /* index of first code of length len in symbols list */

    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        if (it->index >= it->array->nbits) {
            if (len != 1)
                PyErr_SetString(PyExc_ValueError,
                                "incomplete prefix code at end of bitarray");
            return NULL;
        }
        code |= getbit(it->array, it->index++);
        count = it->count[len];
        if (code - count < first) {   /* if length len, return symbol */
            symbol = PyList_GetItem(it->symbols, index + (code - first));
            if (symbol == NULL)
                return NULL;
            Py_INCREF(symbol);
            return symbol;
        }
        index += count;               /* else update for next length */
        first += count;
        first <<= 1;
        code <<= 1;
    }
    PyErr_SetString(PyExc_ValueError, "ran out of codes");
    return NULL;
}

static void
chdi_dealloc(chdi_obj *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->array);
    Py_DECREF(it->symbols);
    PyObject_GC_Del(it);
}

static int
chdi_traverse(chdi_obj *it, visitproc visit, void *arg)
{
    Py_VISIT(it->array);
    return 0;
}

static PyTypeObject CHDI_Type = {
#ifdef IS_PY3K
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size */
#endif
    "bitarray.util.canonical_decodeiter",     /* tp_name */
    sizeof(chdi_obj),                         /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) chdi_dealloc,                /* tp_dealloc */
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
    (traverseproc) chdi_traverse,             /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) chdi_next,                 /* tp_iternext */
    0,                                        /* tp_methods */
};

/* --------------------------------------------------------------------- */

/* Set bitarray_type_obj (bato).  This function must be called before any
   other Python function in this module. */
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
    {"vl_encode", (PyCFunction) vl_encode, METH_O,       vl_encode_doc},
    {"_vl_decode",(PyCFunction) vl_decode, METH_VARARGS, 0},
    {"canonical_decode",
                  (PyCFunction) chdi_new,  METH_VARARGS, chdi_doc},
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
