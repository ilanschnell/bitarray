/*
   Copyright (c) 2019 - 2023, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   This file contains the C implementation of some useful utility functions.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "bitarray.h"

/* set during module initialization */
static PyObject *bitarray_type_obj;

/* Return 0 if obj is bitarray.  If not, set exception and return -1. */
static int
ensure_bitarray(PyObject *obj)
{
    int t;

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

/* ------------------------------- count_n ----------------------------- */

/* Return the smallest index i for which a.count(vi, 0, i) == n.
   When n exceeds the total count, return -1.  */
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
    bitarrayobject *a;
    Py_ssize_t n, i;
    int vi = 1;

    if (!PyArg_ParseTuple(args, "O!n|O&:count_n", bitarray_type_obj,
                          (PyObject *) &a, &n, conv_pybit, &vi))
        return NULL;
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "non-negative integer expected");
        return NULL;
    }
    if (n > a->nbits)  {
        PyErr_SetString(PyExc_ValueError, "n larger than bitarray size");
        return NULL;
    }
    i = count_to_n(a, n, vi);        /* do actual work here */

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
    bitarrayobject *a;
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, res;
    int vi = 1;

    if (!PyArg_ParseTuple(args, "O!|O&nn:rindex", bitarray_type_obj,
                          (PyObject *) &a, conv_pybit, &vi, &start, &stop))
        return NULL;

    adjust_indices(a->nbits, &start, &stop, 1);
    if ((res = find_last(a, vi, start, stop)) < 0)
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
parity(PyObject *module, PyObject *obj)
{
    bitarrayobject *a;
    unsigned char par = 0;
    Py_ssize_t i;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    for (i = 0; i < a->nbits / 8; i++)
        par ^= a->ob_item[i];
    if (a->nbits % 8)
        par ^= zeroed_last_byte(a);

    return PyLong_FromLong((long) bitcount_lookup[par] % 2);
}

PyDoc_STRVAR(parity_doc,
"parity(a, /) -> int\n\
\n\
Return the parity of bitarray `a`.\n\
`parity(a)` is equivalent to `a.count() % 2` but more efficient.");

/* --------------------------- binary functions ------------------------ */

static int
same_size_endian(bitarrayobject *a, bitarrayobject *b)
{
    if (a->nbits != b->nbits) {
        PyErr_SetString(PyExc_ValueError,
                        "bitarrays of equal length expected");
        return -1;
    }
    if (a->endian != b->endian) {
        PyErr_SetString(PyExc_ValueError,
                        "bitarrays of equal endianness expected");
        return -1;
    }
    return 0;
}

static PyObject *
binary_function(PyObject *args, const char *format, const char oper)
{
    Py_ssize_t cnt = 0, s, i;
    bitarrayobject *a, *b;
    unsigned char *buff_a, *buff_b;
    int r;

    if (!PyArg_ParseTuple(args, format,
                          bitarray_type_obj, (PyObject *) &a,
                          bitarray_type_obj, (PyObject *) &b))
        return NULL;
    if (same_size_endian(a, b) < 0)
        return NULL;

    buff_a = (unsigned char *) a->ob_item;
    buff_b = (unsigned char *) b->ob_item;
    s = a->nbits / 8;       /* number of whole bytes in buffer */
    r = a->nbits % 8;       /* remaining bits  */

    switch (oper) {
#define UZ(x)  ((unsigned char) zeroed_last_byte(x))
    case '&':                   /* count and */
        for (i = 0; i < s; i++)
            cnt += bitcount_lookup[buff_a[i] & buff_b[i]];
        if (r)
            cnt += bitcount_lookup[UZ(a) & UZ(b)];
        break;

    case '|':                   /* count or */
        for (i = 0; i < s; i++)
            cnt += bitcount_lookup[buff_a[i] | buff_b[i]];
        if (r)
            cnt += bitcount_lookup[UZ(a) | UZ(b)];
        break;

    case '^':                   /* count xor */
        for (i = 0; i < s; i++)
            cnt += bitcount_lookup[buff_a[i] ^ buff_b[i]];
        if (r)
            cnt += bitcount_lookup[UZ(a) ^ UZ(b)];
        break;

    case 'a':                   /* any and */
        for (i = 0; i < s; i++) {
            if (buff_a[i] & buff_b[i])
                Py_RETURN_TRUE;
        }
        return PyBool_FromLong(r && (UZ(a) & UZ(b)));

    case 's':                   /* is subset */
        for (i = 0; i < s; i++) {
            if ((buff_a[i] & buff_b[i]) != buff_a[i])
                Py_RETURN_FALSE;
        }
        return PyBool_FromLong(r == 0 || (UZ(a) & UZ(b)) == UZ(a));

    default:
        Py_UNREACHABLE();
#undef UZ
    }
    return PyLong_FromSsize_t(cnt);
}

#define COUNT_FUNC(oper, ostr)                                          \
static PyObject *                                                       \
count_ ## oper (PyObject *module, PyObject *args)                       \
{                                                                       \
    return binary_function(args, "O!O!:count_" #oper, *ostr);           \
}                                                                       \
PyDoc_STRVAR(count_ ## oper ## _doc,                                    \
"count_" #oper "(a, b, /) -> int\n\
\n\
Return `(a " ostr " b).count()` in a memory efficient manner,\n\
as no intermediate bitarray object gets created.")

COUNT_FUNC(and, "&");           /* count_and */
COUNT_FUNC(or,  "|");           /* count_or  */
COUNT_FUNC(xor, "^");           /* count_xor */


static PyObject *
any_and(PyObject *module, PyObject *args)
{
    return binary_function(args, "O!O!:any_and", 'a');
}

PyDoc_STRVAR(any_and_doc,
"any_and(a, b, /) -> bool\n\
\n\
Efficient implementation of `any(a & b)`.");


static PyObject *
subset(PyObject *module, PyObject *args)
{
    return binary_function(args, "O!O!:subset", 's');
}

PyDoc_STRVAR(subset_doc,
"subset(a, b, /) -> bool\n\
\n\
Return `True` if bitarray `a` is a subset of bitarray `b`.\n\
`subset(a, b)` is equivalent to `a | b == b` (and equally `a & b == a`) but\n\
more efficient as no intermediate bitarray object is created and the buffer\n\
iteration is stopped as soon as one mismatch is found.");


static PyObject *
correspond_all(PyObject *module, PyObject *args)
{
    Py_ssize_t nff = 0, nft = 0, ntf = 0, ntt = 0, i, s;
    bitarrayobject *a, *b;
    unsigned char u, v, not_u, not_v;

    if (!PyArg_ParseTuple(args, "O!O!:_correspond_all",
                          bitarray_type_obj, (PyObject *) &a,
                          bitarray_type_obj, (PyObject *) &b))
        return NULL;
    if (same_size_endian(a, b) < 0)
        return NULL;
    s = a->nbits / 8;       /* number of whole bytes in buffer */

    for (i = 0; i < s; i++) {
        u = a->ob_item[i];
        v = b->ob_item[i];
        not_u = ~u;
        not_v = ~v;
        nff += bitcount_lookup[not_u & not_v];
        nft += bitcount_lookup[not_u & v];
        ntf += bitcount_lookup[u & not_v];
        ntt += bitcount_lookup[u & v];
    }
    if (a->nbits % 8) {
        unsigned char mask = ones_table[IS_BE(a)][a->nbits % 8];
        u = a->ob_item[s];
        v = b->ob_item[s];
        not_u = ~u;
        not_v = ~v;
        nff += bitcount_lookup[not_u & not_v & mask];
        nft += bitcount_lookup[not_u & v & mask];
        ntf += bitcount_lookup[u & not_v & mask];
        ntt += bitcount_lookup[u & v & mask];
    }
    return Py_BuildValue("nnnn", nff, nft, ntf, ntt);
}

PyDoc_STRVAR(correspond_all_doc,
"_correspond_all(a, b, /) -> tuple\n\
\n\
Return tuple with counts of: ~a & ~b, ~a & b, a & ~b, a & b");

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
    *str = (char) (16 * IS_BE(aa) + set_padbits(aa));
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
ba2hex(PyObject *module, PyObject *obj)
{
    PyObject *result;
    bitarrayobject *a;
    size_t i, strsize;
    char *str;
    int le, be;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    if (a->nbits % 4) {
        PyErr_SetString(PyExc_ValueError, "bitarray length not multiple of 4");
        return NULL;
    }

    /* We want strsize to be even, such that we can transform the entire
       bitarray buffer at once.  Hence, we don't use a->nbits / 4 here, as
       is could make strsize odd. */
    strsize = 2 * Py_SIZE(obj);
    str = (char *) PyMem_Malloc(strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    le = IS_LE(a);
    be = IS_BE(a);
    for (i = 0; i < strsize; i += 2) {
        unsigned char c = a->ob_item[i / 2];

        str[i + le] = hexdigits[c >> 4];
        str[i + be] = hexdigits[0x0f & c];
    }
    assert((size_t) a->nbits / 4 <= strsize);
    result = Py_BuildValue("s#", str, a->nbits / 4);
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
    bitarrayobject *a;
    char *str;
    Py_ssize_t i, strsize;
    int le, be;

    if (!PyArg_ParseTuple(args, "O!s#", bitarray_type_obj, (PyObject *) &a,
                          &str, &strsize))
        return NULL;

    if (a->nbits != 4 * strsize) {
        PyErr_SetString(PyExc_ValueError, "size mismatch");
        return NULL;
    }
    le = IS_LE(a);
    be = IS_BE(a);
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
        a->ob_item[i / 2] = x << 4 | y;
    }
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
    bitarrayobject *a;
    PyObject *result;
    size_t i, strsize;
    char *str;
    int n, m, le;

    if (!PyArg_ParseTuple(args, "iO!:ba2base", &n,
                          bitarray_type_obj, (PyObject *) &a))
        return NULL;
    if ((m = base_to_length(n)) < 0)
        return NULL;

    switch (n) {
    case 32: alphabet = base32_alphabet; break;
    case 64: alphabet = base64_alphabet; break;
    default: alphabet = hexdigits;
    }

    if (a->nbits % m)
        return PyErr_Format(PyExc_ValueError,
                            "bitarray length must be multiple of %d", m);

    strsize = a->nbits / m;
    if ((str = (char *) PyMem_Malloc(strsize)) == NULL)
        return PyErr_NoMemory();

    le = IS_LE(a);
    for (i = 0; i < strsize; i++) {
        int j, x = 0;

        for (j = 0; j < m; j++) {
            int k = le ? j : (m - j - 1);
            x |= getbit(a, i * m + k) << j;
        }
        str[i] = alphabet[x];
    }
    result = Py_BuildValue("s#", str, strsize);
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


/* Translate ASCII digits into bitarray.
   The (Python) arguments to this functions are:
   - base n, one of 2, 4, 8, 16, 32, 64  (n=2^m   where m bits per digit)
   - bitarray (of length m * len(s)) whose elements are overwritten
   - byte object s containing the ASCII digits
*/
static PyObject *
base2ba(PyObject *module, PyObject *args)
{
    bitarrayobject *a = NULL;
    Py_ssize_t i, strsize = 0;
    char *str = NULL;
    int n, m, le;

    if (!PyArg_ParseTuple(args, "i|O!s#", &n, bitarray_type_obj,
                          (PyObject *) &a, &str, &strsize))
        return NULL;
    if ((m = base_to_length(n)) < 0)
        return NULL;
    if (a == NULL)  /* when only base n is given - return length log2(n) */
        return PyLong_FromLong(m);

    if (a->nbits != m * strsize) {
        PyErr_SetString(PyExc_ValueError, "size mismatch");
        return NULL;
    }
    le = IS_LE(a);
    for (i = 0; i < strsize; i++) {
        int j, d = digit_to_int(n, str[i]);

        if (d < 0) {
            unsigned char c = str[i];
            return PyErr_Format(PyExc_ValueError, "invalid digit found for "
                                "base %d, got '%c' (0x%02x)", n, c, c);
        }
        for (j = 0; j < m; j++) {
            int k = le ? j : (m - j - 1);
            setbit(a, i * m + k, d & (1 << j));
        }
    }
    Py_RETURN_NONE;
}

/* -------------------------- utility functions ------------------------ */

/* like resize() */
static int
resize_lite(bitarrayobject *self, Py_ssize_t nbits)
{
    const Py_ssize_t allocated = self->allocated, size = Py_SIZE(self);
    const Py_ssize_t newsize = BYTES(nbits);
    size_t new_allocated;

    assert(allocated >= size && size == BYTES(self->nbits));
    assert(self->ob_exports == 0);
    assert(self->buffer == NULL);
    assert(self->readonly == 0);

    if (newsize == size) {
        /* buffer size hasn't changed - bypass everything */
        self->nbits = nbits;
        return 0;
    }

    /* Bypass reallocation when a allocation is large enough to accommodate
       the newsize.  In the newsize falls lower than the current size,
       then proceed with the reallocation to shrink the bitarray.
    */
    if (allocated >= newsize && newsize > size) {
        Py_SET_SIZE(self, newsize);
        self->nbits = nbits;
        return 0;
    }

    if (newsize > size)  /* only over-allocate when size increases */
        new_allocated = ((size_t) newsize + (newsize >> 4) +
                         (newsize < 8 ? 3 : 7)) & ~(size_t) 3;
    else
        new_allocated = ((size_t) newsize + 3) & ~(size_t) 3;

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

/* Consume one byte from the iteratior and return it's value as an integer
   in range(256).  On failure, set an exception and return -1.  */
static int
next_char(PyObject *iter)
{
    PyObject *item;
    unsigned char c;

    item = PyIter_Next(iter);
    if (item == NULL) {
        if (PyErr_Occurred())   /* from PyIter_Next() */
            return -1;
        PyErr_SetString(PyExc_ValueError, "unexpected end of stream");
        return -1;
    }

#ifdef IS_PY3K
    if (PyLong_Check(item))
        c = (unsigned char) PyLong_AsLong(item);
#else
    if (PyBytes_Check(item))
        c = (unsigned char) *PyBytes_AS_STRING(item);
#endif
    else {
        PyErr_Format(PyExc_TypeError, "int iterator expected, "
                     "got '%s' element", Py_TYPE(item)->tp_name);
        Py_DECREF(item);
        return -1;
    }
    Py_DECREF(item);
    return (int) c;
}

/* ---------------------- sparse compressed bitarray ------------------- */

/* Encode m bytes (up to 32) of a's buffer (starting at offset) and write
   to buffer str.  Return number of bytes written into str.  */
static int
sc_encode_block(char *str, bitarrayobject *a, Py_ssize_t offset,
                int m, int last)
{
    int cnt = 0, raw, i, j, k, len = 0;
    char *buff = a->ob_item + offset;

    assert(m <= 32 && (last == 0 || last == 1));
    for (i = 0; i < m; i++)
        cnt += bitcount_lookup[(unsigned char) buff[i]];

    raw = cnt >= m;
    k = raw ? m : cnt;      /* bytes in output block (without head) */
    str[len++] = last << 7 | raw << 6 | k;

    if (raw) {
        memcpy(str + len, a->ob_item + offset, (size_t) k);
        len += k;
    }
    else if (cnt) {
        for (i = 0; i < m; i++)
            if (buff[i]) {
                for (j = 0; j < 8; j++)
                    if (buff[i] & BITMASK(a, j))
                        str[len++] = 8 * i + j;
            }
    }
    assert(len == k + 1);
    return len;
}

static PyObject *
sc_encode(PyObject *module, PyObject *obj)
{
    PyObject *out;
    bitarrayobject *a;
    Py_ssize_t offset, len = 0;
    char *str;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;

    out = PyBytes_FromStringAndSize(NULL, 2);
    if (out == NULL)
        return NULL;

    str = PyBytes_AsString(out);
    str[len++] = IS_BE(a) ? 'B' : 'L';
    str[len++] = a->nbits % 256;

    set_padbits(a);
    for (offset = 0;; offset += 32) {
        int n, last;

        if (_PyBytes_Resize(&out, len + 33) < 0)
            return NULL;
        str = PyBytes_AsString(out);

        n = Py_MIN(a->nbits - 8 * offset, 256);  /* block size in bits */
        last = n < 256 || 8 * offset + 256 == a->nbits;
        len += sc_encode_block(str + len, a, offset, BYTES(n), last);
        if (last)
            break;
    }
    if (_PyBytes_Resize(&out, len) < 0)
        return NULL;

    return out;
}

PyDoc_STRVAR(sc_encode_doc,
"sc_encode(bitarray, /) -> bytes\n\
\n\
Compress a sparse bitarray and return its binary representation.\n\
This representation is useful for efficiently storing sparse bitarrays.\n\
Use `sc_decode()` for decoding.");


/* decode one block - consume iter and extend bitarray a */
static int
sc_decode_block(bitarrayobject *a, PyObject *iter)
{
    Py_ssize_t offset;
    int last, raw, c, i, k;

    if ((c = next_char(iter)) < 0)
        return -1;

    last = c & 0x80;            /* is this the last block? */
    raw = c & 0x40;             /* is this a raw block? */
    k = c & 0x3f;               /* bytes to read from stream */

    if (k > (raw ? 32 : 31) || (!last && raw && k != 32)) {
        PyErr_Format(PyExc_ValueError, "invalid header byte: 0x%02x", c);
        return -1;
    }
    assert(a->nbits % 256 == 0);
    offset = a->nbits / 8;      /* buffer offset in bytes */

    if (resize_lite(a, a->nbits + (raw ? (8 * k) : 256)) < 0)
        return -1;

    if (!raw)
        memset(a->ob_item + offset, 0x00, (size_t) 32);

    for (i = 0; i < k; i++) {
        if ((c = next_char(iter)) < 0)
            return -1;

        if (raw)
            a->ob_item[i + offset] = (unsigned char) c;
        else
            setbit(a, 8 * offset + c, 1);
    }
    return last;
}

static int
sc_read_header(PyObject *iter, int *endian, int *last_nbits)
{
    int c;

    if ((c = next_char(iter)) < 0)
        return -1;

    switch (c) {
    case 'B':
        *endian = ENDIAN_BIG;
        break;
    case 'L':
        *endian = ENDIAN_LITTLE;
        break;
    default:
        PyErr_Format(PyExc_ValueError, "invalid header byte: 0x%02x", c);
        return -1;
    }

    *last_nbits = next_char(iter);
    if (*last_nbits < 0)
        return -1;

    return 0;
}

static PyObject *
sc_decode(PyObject *module, PyObject *args)
{
    PyObject *iter;
    bitarrayobject *a;
    int endian, last_nbits;

    if (!PyArg_ParseTuple(args, "OO!", &iter,
                          bitarray_type_obj, (PyObject *) &a))
        return NULL;
    if (!PyIter_Check(iter))
        return PyErr_Format(PyExc_TypeError, "iterator or bytes expected, "
                            "got '%s'", Py_TYPE(iter)->tp_name);

    if (a->nbits || a->readonly || a->buffer) {
        PyErr_SetString(PyExc_SystemError, "empty writable bitarray expected");
        return NULL;
    }
    if (sc_read_header(iter, &endian, &last_nbits) < 0)
        return NULL;

    a->endian = endian;

    while (1) {
        int last = sc_decode_block(a, iter);
        if (last < 0)
            return NULL;
        if (last)
            break;
    }
    if (last_nbits) {           /* shrink to actual size */
        Py_ssize_t full_blocks = (a->nbits - 1) / 256;
        Py_ssize_t new_nbits = 256 * full_blocks + last_nbits;
        assert(a->nbits > 0 && new_nbits <= a->nbits);
        resize_lite(a, new_nbits);
    }
    Py_RETURN_NONE;
}

/* ------------------- variable length bitarray format ----------------- */

/* LEN_PAD_BITS is always 3 - the number of bits (length) that is necessary to
   represent the number of pad bits.  The number of padding bits itself is
   called 'padding' below.

   'padding' refers to the pad bits within the variable length format.
   This is not the same as the pad bits of the actual bitarray.
   For example, b'\x10' has padding = 1, and decodes to bitarray('000'),
   which has 5 pad bits.  'padding' can take values to up 6.
 */
#define LEN_PAD_BITS  3

/* consume iterator while decoding bytes into bitarray */
static PyObject *
vl_decode(PyObject *module, PyObject *args)
{
    PyObject *iter;
    bitarrayobject *a;
    Py_ssize_t padding = 0;  /* number of pad bits read from header byte */
    Py_ssize_t i = 0;        /* bit counter */

    if (!PyArg_ParseTuple(args, "OO!", &iter,
                          bitarray_type_obj, (PyObject *) &a))
        return NULL;
    if (!PyIter_Check(iter))
        return PyErr_Format(PyExc_TypeError, "iterator or bytes expected, "
                            "got '%s'", Py_TYPE(iter)->tp_name);

    while (1) {
        int k, b;

        if ((b = next_char(iter)) < 0)
            return NULL;

        if (i + 6 >= a->nbits && resize_lite(a, i + 7) < 0)
            return NULL;
        assert(i + 6 < a->nbits);

        if (i == 0) {
            padding = (b & 0x70) >> 4;
            if (padding >= 7 || ((b & 0x80) == 0 && padding > 4))
                return PyErr_Format(PyExc_ValueError,
                                    "invalid header byte: 0x%02x", b);
            for (k = 0; k < 4; k++)
                setbit(a, i++, (0x08 >> k) & b);
        }
        else {
            for (k = 0; k < 7; k++)
                setbit(a, i++, (0x40 >> k) & b);
        }
        if ((b & 0x80) == 0)
            break;
    }
    /* set final length of bitarray */
    resize_lite(a, i - padding);

    Py_RETURN_NONE;
}

static PyObject *
vl_encode(PyObject *module, PyObject *obj)
{
    PyObject *result;
    bitarrayobject *a;
    Py_ssize_t padding, n, m, i;
    Py_ssize_t j = 0;           /* byte conter */
    char *str;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    n = (a->nbits + LEN_PAD_BITS + 6) / 7;  /* number of resulting bytes */
    m = 7 * n - LEN_PAD_BITS;    /* number of bits resulting bytes can hold */
    padding = m - a->nbits;      /* number of pad bits */
    assert(0 <= padding && padding < 7);

    result = PyBytes_FromStringAndSize(NULL, n);
    if (result == NULL)
        return NULL;

    str = PyBytes_AsString(result);
    str[0] = a->nbits > 4 ? 0x80 : 0x00;   /* leading bit */
    str[0] |= padding << 4;                /* encode padding */
    for (i = 0; i < 4 && i < a->nbits; i++)
        str[0] |= (0x08 >> i) * getbit(a, i);

    for (i = 4; i < a->nbits; i++) {
        int k = (i - 4) % 7;

        if (k == 0) {
            j++;
            str[j] = j < n - 1 ? 0x80 : 0x00;  /* leading bit */
        }
        str[j] |= (0x40 >> k) * getbit(a, i);
    }
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

/*
   The decode iterator object includes the Huffman code decoding tables:
   - count[1..MAXBITS] is the number of symbols of each length, which for a
     canonical code are stepped through in order.  count[0] is not used.
   - symbol is a Python sequence of the symbols in canonical order
     where the number of entries is the sum of the counts in count[].
 */
#define MAXBITS  31                  /* maximum bits in a code */

typedef struct {
    PyObject_HEAD
    bitarrayobject *array;           /* bitarray we're decoding */
    Py_ssize_t index;                /* current index in bitarray */
    int count[MAXBITS + 1];          /* number of symbols of each length */
    PyObject *symbol;                /* canonical ordered symbols */
} chdi_obj;                          /* canonical Huffman decode iterator */

static PyTypeObject CHDI_Type;

/* set elements in count (from seq) and return their sum, or -1 on error */
static Py_ssize_t
set_count(int *count, PyObject *sequence)
{
    Py_ssize_t n, c, res = 0;
    int i;

    n = PySequence_Size(sequence);
    if (n < 0)
        return -1;

    if (n > MAXBITS) {
        PyErr_Format(PyExc_ValueError, "len(count) cannot be larger than %d",
                     MAXBITS);
        return -1;
    }

    for (i = 1; i <= MAXBITS; i++) {
        c = 0;
        if (i < n) {
            PyObject *item = PySequence_GetItem(sequence, i);
            Py_ssize_t maxcount = ((Py_ssize_t) 1) << i;

            if (item == NULL)
                return -1;
            c = PyNumber_AsSsize_t(item, PyExc_OverflowError);
            Py_DECREF(item);
            if (c == -1 && PyErr_Occurred())
                return -1;
            if (c < 0 || c > maxcount) {
                PyErr_Format(PyExc_ValueError, "count[%d] cannot be negative"
                             " or larger than %zd, got %zd", i, maxcount, c);
                return -1;
            }
        }
        count[i] = (int) c;
        res += c;
    }
    return res;
}

/* create a new initialized canonical Huffman decode iterator object */
static PyObject *
chdi_new(PyObject *module, PyObject *args)
{
    PyObject *a, *count, *symbol;
    Py_ssize_t count_sum;
    chdi_obj *it;       /* iterator object to be returned */

    if (!PyArg_ParseTuple(args, "O!OO:count_n",
                          bitarray_type_obj, &a, &count, &symbol))
        return NULL;
    if (!PySequence_Check(count))
        return PyErr_Format(PyExc_TypeError, "count expected to be sequence, "
                            "got '%s'", Py_TYPE(count)->tp_name);

    symbol = PySequence_Fast(symbol, "symbol not iterable");
    if (symbol == NULL)
        return NULL;

    it = PyObject_GC_New(chdi_obj, &CHDI_Type);
    if (it == NULL)
        goto error;

    if ((count_sum = set_count(it->count, count)) < 0)
        goto error;

    if (count_sum != PySequence_Size(symbol)) {
        PyErr_Format(PyExc_ValueError, "sum(count) = %zd, but len(symbol) "
                     "= %zd", count_sum, PySequence_Size(symbol));
        goto error;
    }
    Py_INCREF(a);
    it->array = (bitarrayobject *) a;
    it->index = 0;
    /* PySequence_Fast() returns a new reference, so no Py_INCREF here */
    it->symbol = symbol;

    PyObject_GC_Track(it);
    return (PyObject *) it;

 error:
    it->array = NULL;
    Py_XDECREF(symbol);
    it->symbol = NULL;
    Py_DECREF((PyObject *) it);
    return NULL;
}

PyDoc_STRVAR(chdi_doc,
"canonical_decode(bitarray, count, symbol, /) -> iterator\n\
\n\
Decode bitarray using canonical Huffman decoding tables\n\
where `count` is a sequence containing the number of symbols of each length\n\
and `symbol` is a sequence of symbols in canonical order.");

/* This function is based on the function decode() in:
   https://github.com/madler/zlib/blob/master/contrib/puff/puff.c */
static PyObject *
chdi_next(chdi_obj *it)
{
    Py_ssize_t nbits = it->array->nbits;
    int len;    /* current number of bits in code */
    int code;   /* current code (of len bits) */
    int first;  /* first code of length len */
    int count;  /* number of codes of length len */
    int index;  /* index of first code of length len in symbol list */

    if (it->index >= nbits)           /* no bits - stop iteration */
        return NULL;

    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= getbit(it->array, it->index++);
        count = it->count[len];
        assert(code - first >= 0);
        if (code - first < count) {   /* if length len, return symbol */
            return PySequence_ITEM(it->symbol, index + (code - first));
        }
        index += count;               /* else update for next length */
        first += count;
        first <<= 1;
        code <<= 1;

        if (it->index >= nbits && len != MAXBITS) {
            PyErr_SetString(PyExc_ValueError, "reached end of bitarray");
            return NULL;
        }
    }
    PyErr_SetString(PyExc_ValueError, "ran out of codes");
    return NULL;
}

static void
chdi_dealloc(chdi_obj *it)
{
    PyObject_GC_UnTrack(it);
    Py_XDECREF(it->array);
    Py_XDECREF(it->symbol);
    PyObject_GC_Del(it);
}

static int
chdi_traverse(chdi_obj *it, visitproc visit, void *arg)
{
    Py_VISIT(it->array);
    Py_VISIT(it->symbol);
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

static PyMethodDef module_functions[] = {
    {"count_n",   (PyCFunction) count_n,   METH_VARARGS, count_n_doc},
    {"rindex",    (PyCFunction) r_index,   METH_VARARGS, rindex_doc},
    {"parity",    (PyCFunction) parity,    METH_O,       parity_doc},
    {"count_and", (PyCFunction) count_and, METH_VARARGS, count_and_doc},
    {"count_or",  (PyCFunction) count_or,  METH_VARARGS, count_or_doc},
    {"count_xor", (PyCFunction) count_xor, METH_VARARGS, count_xor_doc},
    {"any_and",   (PyCFunction) any_and,   METH_VARARGS, any_and_doc},
    {"subset",    (PyCFunction) subset,    METH_VARARGS, subset_doc},
    {"_correspond_all",
                  (PyCFunction) correspond_all,
                                           METH_VARARGS, correspond_all_doc},
    {"serialize", (PyCFunction) serialize, METH_O,       serialize_doc},
    {"ba2hex",    (PyCFunction) ba2hex,    METH_O,       ba2hex_doc},
    {"_hex2ba",   (PyCFunction) hex2ba,    METH_VARARGS, 0},
    {"ba2base",   (PyCFunction) ba2base,   METH_VARARGS, ba2base_doc},
    {"_base2ba",  (PyCFunction) base2ba,   METH_VARARGS, 0},
    {"sc_encode", (PyCFunction) sc_encode, METH_O,       sc_encode_doc},
    {"_sc_decode",(PyCFunction) sc_decode, METH_VARARGS, 0},
    {"vl_encode", (PyCFunction) vl_encode, METH_O,       vl_encode_doc},
    {"_vl_decode",(PyCFunction) vl_decode, METH_VARARGS, 0},
    {"canonical_decode",
                  (PyCFunction) chdi_new,  METH_VARARGS, chdi_doc},
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
    PyObject *m, *bitarray_module;

    if ((bitarray_module = PyImport_ImportModule("bitarray")) == NULL)
        goto error;
    bitarray_type_obj = PyObject_GetAttrString(bitarray_module, "bitarray");
    Py_DECREF(bitarray_module);
    if (bitarray_type_obj == NULL)
        goto error;

#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3("_util", module_functions, 0);
#endif
    if (m == NULL)
        goto error;

    if (PyType_Ready(&CHDI_Type) < 0)
        goto error;
    Py_SET_TYPE(&CHDI_Type, &PyType_Type);

#ifdef IS_PY3K
    return m;
 error:
    return NULL;
#else
 error:
    return;
#endif
}
