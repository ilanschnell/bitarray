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
        PyErr_Format(PyExc_TypeError, "bitarray expected, not '%s'",
                     Py_TYPE(obj)->tp_name);
        return -1;
    }
    return 0;
}

/* return new bitarray of length `nbits` (with uninitialized buffer) and
   endianness given by the PyObject 'endian' (which may be Py_None) */
static bitarrayobject *
new_bitarray(Py_ssize_t nbits, PyObject *endian)
{
    PyObject *args;             /* args for bitarray() */
    bitarrayobject *res;

    if ((args = PyTuple_New(2)) == NULL)
        return NULL;

    /* PyTuple_SET_ITEM "steals" a reference to item */
    PyTuple_SET_ITEM(args, 0, PyLong_FromSsize_t(nbits));
    Py_INCREF(endian);
    PyTuple_SET_ITEM(args, 1, endian);

    res = (bitarrayobject *) PyObject_CallObject(bitarray_type_obj, args);
    Py_DECREF(args);
    if (res == NULL)
        return NULL;
    assert(res->nbits == nbits && res->readonly == 0 && res->buffer == NULL);

    return res;
}

/* Starting from word index `i`, count the remaining population in bitarray
   buffer.  Equivalent to:  a[64 * i:].count()  */
static Py_ssize_t
count_from_word(bitarrayobject *a, Py_ssize_t i)
{
    assert(i >= 0);
    if (64 * i >= a->nbits)
        return 0;

    return popcnt_words(WBUFF(a) + i, a->nbits / 64 - i) + popcnt_64(zlw(a));
}

/* -------------------------------- zeros ------------------------------ */

static PyObject *
zeros(PyObject *module, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "endian", NULL};
    PyObject *endian = Py_None;
    bitarrayobject *a = NULL;
    Py_ssize_t nbits;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "n|O:zeros", kwlist,
                                     &nbits, &endian))
        return NULL;

    a = new_bitarray(nbits, endian);
    if (a == NULL)
        return NULL;

    memset(a->ob_item, 0x00, (size_t) Py_SIZE(a));

    return (PyObject *) a;
}

PyDoc_STRVAR(zeros_doc,
"zeros(length, /, endian=None) -> bitarray\n\
\n\
Create a bitarray of length, with all values 0, and optional\n\
endianness, which may be 'big', 'little'.");

/* ------------------------------- count_n ----------------------------- */

/* Return the smallest index i for which a.count(vi, 0, i) == n.
   When n exceeds the total count, the result is a negative
   number; the negative of the total count + 1, which is useful
   for displaying the error message. */
static Py_ssize_t
count_n_core(bitarrayobject *a, Py_ssize_t n, int vi)
{
    const Py_ssize_t nbits = a->nbits;
    uint64_t *wbuff = WBUFF(a);
    Py_ssize_t i = 0;         /* index (result) */
    Py_ssize_t t = 0;         /* total count up to index */
    Py_ssize_t m;             /* popcount in each block */

    assert(0 <= n && n <= nbits);

    /* by counting big blocks we save comparisons and updates */
#define BLOCK_BITS  4096      /* block size: 4096 bits = 64 words */
    while (i + BLOCK_BITS < nbits) {
        m = popcnt_words(wbuff + i / 64, BLOCK_BITS / 64);
        if (!vi)
            m = BLOCK_BITS - m;
        if (t + m >= n)
            break;
        t += m;
        i += BLOCK_BITS;
    }
#undef BLOCK_BITS

    while (i + 64 < nbits) {  /* count blocks of single (64-bit) words */
        m = popcnt_64(wbuff[i / 64]);
        if (!vi)
            m = 64 - m;
        if (t + m >= n)
            break;
        t += m;
        i += 64;
    }

    while (i < nbits && t < n) {
        t += getbit(a, i) == vi;
        i++;
    }

    if (t < n) {  /* n exceeds total count */
        assert((vi ? t : nbits - t) == count_from_word(a, 0));
        return -(t + 1);
    }
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
    if (n > a->nbits)
        return PyErr_Format(PyExc_ValueError, "n = %zd larger than bitarray "
                            "size (len(a) = %zd)", n, a->nbits);

    i = count_n_core(a, n, vi);        /* do actual work here */

    if (i < 0)
        return PyErr_Format(PyExc_ValueError, "n = %zd exceeds total count "
                            "(a.count(%d) = %zd)", n, vi, -i - 1);

    return PyLong_FromSsize_t(i);
}

PyDoc_STRVAR(count_n_doc,
"count_n(a, n, value=1, /) -> int\n\
\n\
Return lowest index `i` for which `a[:i].count(value) == n`.\n\
Raises `ValueError` when `n` exceeds total count (`a.count(value)`).");

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
    if (n > 64) {
        const Py_ssize_t wa = (a + 63) / 64;  /* word range(wa, wb) */
        const Py_ssize_t wb = b / 64;
        const uint64_t *wbuff = WBUFF(self);
        const uint64_t w = vi ? 0 : ~0;

        if ((res = find_last(self, vi, 64 * wb, b)) >= 0)
            return res;

        for (i = wb - 1; i >= wa; i--) {  /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_last(self, vi, 64 * i, 64 * i + 64);
        }
        return find_last(self, vi, a, 64 * wa);
    }

    if (n > 8) {
        const Py_ssize_t byte_a = BYTES(a);  /* byte range(byte_a, byte_b) */
        const Py_ssize_t byte_b = b / 8;
        const char *buff = self->ob_item;
        const char c = vi ? 0 : ~0;

        if ((res = find_last(self, vi, 8 * byte_b, b)) >= 0)
            return res;

        for (i = byte_b - 1; i >= byte_a; i--) {  /* skip bytes */
            assert_byte_in_range(self, i);
            if (c ^ buff[i])
                return find_last(self, vi, 8 * i, 8 * i + 8);
        }
        return find_last(self, vi, a, 8 * byte_a);
    }

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
    uint64_t par = 0;
    Py_ssize_t i;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    for (i = 0; i < a->nbits / 64; i++)
        par ^= WBUFF(a)[i];
    if (a->nbits % 64)
        par ^= zlw(a);
    for (i = 32; i > 0; i /= 2)
        par ^= par >> i;
    return PyLong_FromLong(par & 1);
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
    Py_ssize_t cnt = 0, cwords, i;
    bitarrayobject *a, *b;
    uint64_t *wbuff_a, *wbuff_b;
    int rbits;

    if (!PyArg_ParseTuple(args, format,
                          bitarray_type_obj, (PyObject *) &a,
                          bitarray_type_obj, (PyObject *) &b))
        return NULL;
    if (same_size_endian(a, b) < 0)
        return NULL;

    wbuff_a = WBUFF(a);
    wbuff_b = WBUFF(b);
    cwords = a->nbits / 64;     /* number of complete 64-bit words */
    rbits = a->nbits % 64;      /* remaining bits  */

    switch (oper) {
    case '&':                   /* count and */
        for (i = 0; i < cwords; i++)
            cnt += popcnt_64(wbuff_a[i] & wbuff_b[i]);
        if (rbits)
            cnt += popcnt_64(zlw(a) & zlw(b));
        break;

    case '|':                   /* count or */
        for (i = 0; i < cwords; i++)
            cnt += popcnt_64(wbuff_a[i] | wbuff_b[i]);
        if (rbits)
            cnt += popcnt_64(zlw(a) | zlw(b));
        break;

    case '^':                   /* count xor */
        for (i = 0; i < cwords; i++)
            cnt += popcnt_64(wbuff_a[i] ^ wbuff_b[i]);
        if (rbits)
            cnt += popcnt_64(zlw(a) ^ zlw(b));
        break;

    case 'a':                   /* any and */
        for (i = 0; i < cwords; i++) {
            if (wbuff_a[i] & wbuff_b[i])
                Py_RETURN_TRUE;
        }
        return PyBool_FromLong(rbits && (zlw(a) & zlw(b)));

    case 's':                   /* is subset */
        for (i = 0; i < cwords; i++) {
            if ((wbuff_a[i] & wbuff_b[i]) != wbuff_a[i])
                Py_RETURN_FALSE;
        }
        return PyBool_FromLong(rbits == 0 || (zlw(a) & zlw(b)) == zlw(a));

    default:
        Py_UNREACHABLE();
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
    Py_ssize_t nff = 0, nft = 0, ntf = 0, ntt = 0, cwords, i;
    bitarrayobject *a, *b;
    uint64_t u, v, not_u, not_v;
    int rbits;

    if (!PyArg_ParseTuple(args, "O!O!:_correspond_all",
                          bitarray_type_obj, (PyObject *) &a,
                          bitarray_type_obj, (PyObject *) &b))
        return NULL;
    if (same_size_endian(a, b) < 0)
        return NULL;

    cwords = a->nbits / 64;     /* complete 64-bit words */
    rbits = a->nbits % 64;      /* remaining bits */

    for (i = 0; i < cwords; i++) {
        u = WBUFF(a)[i];
        v = WBUFF(b)[i];
        not_u = ~u;
        not_v = ~v;
        nff += popcnt_64(not_u & not_v);
        nft += popcnt_64(not_u & v);
        ntf += popcnt_64(u & not_v);
        ntt += popcnt_64(u & v);
    }

    if (rbits) {
        u = zlw(a);
        v = zlw(b);
        not_u = ~u;
        not_v = ~v;
        /* for nff we need to substract the number of unused 1 bits */
        nff += popcnt_64(not_u & not_v) - (64 - rbits);
        nft += popcnt_64(not_u & v);
        ntf += popcnt_64(u & not_v);
        ntt += popcnt_64(u & v);
    }
    return Py_BuildValue("nnnn", nff, nft, ntf, ntt);
}

PyDoc_STRVAR(correspond_all_doc,
"_correspond_all(a, b, /) -> tuple\n\
\n\
Return tuple with counts of: ~a & ~b, ~a & b, a & ~b, a & b");

/* ---------------------------- serialization -------------------------- */

/*
  The binary format used here is similar to the one used for pickling
  bitarray objects.  This format encodes the bit-endianness in the head byte,
  whereas the binary pickle blob does not.
*/

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
    *str = (IS_BE(aa) ? 0x10 : 0x00) | ((char) set_padbits(aa));
    memcpy(str + 1, aa->ob_item, (size_t) nbytes);
#undef aa
    return result;
}

PyDoc_STRVAR(serialize_doc,
"serialize(bitarray, /) -> bytes\n\
\n\
Return a serialized representation of the bitarray, which may be passed to\n\
`deserialize()`.  It efficiently represents the bitarray object (including\n\
its bit-endianness) and is guaranteed not to change in future releases.");


static PyObject *
deserialize(PyObject *module, PyObject *buffer)
{
    Py_buffer view;
    bitarrayobject *a;
    unsigned char head;

    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    if (view.len == 0) {
        PyErr_SetString(PyExc_ValueError,
                        "non-empty bytes-like object expected");
        goto error;
    }

    head = *((unsigned char *) view.buf);

    if (head & 0xe8 || (view.len == 1 && head & 0xef)) {
        PyErr_Format(PyExc_ValueError, "invalid header byte: 0x%02x", head);
        goto error;
    }
    /* create bitarray of desired length (buffer uninitialized) */
    a = new_bitarray(8 * (view.len - 1) - ((Py_ssize_t) (head & 0x07)),
                     Py_None);
    if (a == NULL)
        goto error;
    /* set bit-endianness and buffer */
    a->endian = head & 0x10 ? ENDIAN_BIG : ENDIAN_LITTLE;
    assert(Py_SIZE(a) == view.len - 1);
    memcpy(a->ob_item, ((char *) view.buf) + 1, (size_t) view.len - 1);

    PyBuffer_Release(&view);
    return (PyObject *) a;
 error:
    PyBuffer_Release(&view);
    return NULL;
}

PyDoc_STRVAR(deserialize_doc,
"deserialize(bytes, /) -> bitarray\n\
\n\
Return a bitarray given a bytes-like representation such as returned\n\
by `serialize()`.");

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


/* Translate hexadecimal digits 'bytes' into the bitarray 'a' buffer.
   Each digit corresponds to 4 bits in the bitarray.
   The number of digits may be odd. */
static int
hex2ba_core(bitarrayobject *a, PyObject *bytes)
{
    Py_ssize_t strsize, i;
    char *str;
    int le = IS_LE(a), be = IS_BE(a);

    assert(PyBytes_Check(bytes));
    str = PyBytes_AS_STRING(bytes);
    strsize = PyBytes_GET_SIZE(bytes);
    assert(a->nbits == 4 * strsize);
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
                return -1;
            }
        }
        assert(0 <= x && x < 16 && 0 <= y && y < 16);
        a->ob_item[i / 2] = x << 4 | y;
    }
    return 0;
}

/* Return new reference to bytes object from either ASCII str (unicode) or
   bytes.  On failure, set exception and return NULL. */
static PyObject *
anystr_to_bytes(PyObject *obj)
{
    PyObject *bytes;

    if (PyUnicode_Check(obj)) {
        bytes = PyUnicode_AsASCIIString(obj);
        if (bytes == NULL)
            return NULL;
    }
    else if (PyBytes_Check(obj)) {
        bytes = obj;
        Py_INCREF(bytes);
    }
    else {
        PyErr_Format(PyExc_TypeError, "str or bytes expected, got '%s'",
                     Py_TYPE(obj)->tp_name);
        return NULL;
    }
    assert(PyBytes_Check(bytes));
    return bytes;
}

static PyObject *
hex2ba(PyObject *module, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "endian", NULL};
    PyObject *obj, *bytes, *endian = Py_None;
    bitarrayobject *a;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:hex2ba", kwlist,
                                     &obj, &endian))
        return NULL;

    if ((bytes = anystr_to_bytes(obj)) == NULL)
        return NULL;

    a = new_bitarray(4 * PyBytes_GET_SIZE(bytes), endian);
    if (a == NULL)
        goto error;

    if (hex2ba_core(a, bytes) < 0)  /* do translation here */
        goto error;

    Py_DECREF(bytes);
    return (PyObject *) a;
 error:
    Py_DECREF(bytes);
    Py_XDECREF((PyObject *) a);
    return NULL;
}

PyDoc_STRVAR(hex2ba_doc,
"hex2ba(hexstr, /, endian=None) -> bitarray\n\
\n\
Bitarray of hexadecimal representation.  hexstr may contain any number\n\
(including odd numbers) of hex digits (upper or lower case).");

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


/* Translate ASCII digits 'bytes' into bitarray buffer.
   The arguments to this functions are:
   - bitarray (of length m * len(s)) whose elements are overwritten
   - byte object s containing the ASCII digits
   - bits per digit, that is the base length m  (1..6)
*/
static int
base2ba_core(bitarrayobject *a, PyObject *bytes, int m)
{
    Py_ssize_t strsize, i;
    char *str;
    int le = IS_LE(a), n = 1 << m;

    assert(PyBytes_Check(bytes));
    str = PyBytes_AS_STRING(bytes);
    strsize = PyBytes_GET_SIZE(bytes);
    assert(a->nbits == m * strsize);

    for (i = 0; i < strsize; i++) {
        int j, d = digit_to_int(n, str[i]);

        if (d < 0) {
            unsigned char c = str[i];
            PyErr_Format(PyExc_ValueError, "invalid digit found for "
                         "base %d, got '%c' (0x%02x)", n, c, c);
            return -1;
        }
        for (j = 0; j < m; j++) {
            int k = le ? j : (m - j - 1);
            setbit(a, i * m + k, d & (1 << j));
        }
    }
    return 0;
}

static PyObject *
base2ba(PyObject *module, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "", "endian", NULL};
    PyObject *obj, *bytes, *endian = Py_None;
    bitarrayobject *a;
    int n, m;                   /* n = 2^m */

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "iO|O:base2ba", kwlist,
                                     &n, &obj, &endian))
        return NULL;

    if ((m = base_to_length(n)) < 0)
        return NULL;

    if ((bytes = anystr_to_bytes(obj)) == NULL)
        return NULL;

    a = new_bitarray(m * PyBytes_GET_SIZE(bytes), endian);
    if (a == NULL)
        goto error;

    if (base2ba_core(a, bytes, m) < 0)  /* do translation here */
        goto error;

    Py_DECREF(bytes);
    return (PyObject *) a;
 error:
    Py_DECREF(bytes);
    Py_XDECREF((PyObject *) a);
    return NULL;
}

PyDoc_STRVAR(base2ba_doc,
"base2ba(n, asciistr, /, endian=None) -> bitarray\n\
\n\
Bitarray of the base `n` ASCII representation.\n\
Allowed values for `n` are 2, 4, 8, 16, 32 and 64.\n\
For `n=16` (hexadecimal), `hex2ba()` will be much faster, as `base2ba()`\n\
does not take advantage of byte level operations.\n\
For `n=32` the RFC 4648 Base32 alphabet is used, and for `n=64` the\n\
standard base 64 alphabet is used.");

/* ------------------------ utility C functions ------------------------ */

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
        self->nbits = nbits;
        return 0;
    }

    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        assert(self->ob_item != NULL || newsize == 0);
        Py_SET_SIZE(self, newsize);
        self->nbits = nbits;
        return 0;
    }

    if (newsize == 0) {
        PyMem_Free(self->ob_item);
        self->ob_item = NULL;
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        return 0;
    }

    new_allocated = ((size_t) newsize + (newsize >> 4) +
                     (newsize < 8 ? 3 : 7)) & ~(size_t) 3;

    if (newsize - size > (Py_ssize_t) new_allocated - newsize)
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

#if IS_PY3K
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

/* write n bytes (into buffer str) representing the integer i (using
   little endian byte-order) */
static void
write_n(char *str, int n, Py_ssize_t i)
{
    int len = 0;

    assert(n <= 8);
    while (len < n) {
        str[len++] = (char) i & 0xff;
        i >>= 8;
    }
    assert(i == 0);
}

/* read n bytes from iter and return the corresponding positive integer,
   using little endian byte-order */
static Py_ssize_t
read_n(int n, PyObject *iter)
{
    Py_ssize_t i = 0;
    int j, c;

    assert(n <= 8);
    for (j = 0; j < n; j++) {
        if ((c = next_char(iter)) < 0)
            return -1;
        i |= ((Py_ssize_t) c) << (8 * j);
    }
    if (i < 0) {
        PyErr_Format(PyExc_ValueError,
                     "read %d bytes got negative value: %zd", n, i);
        return -1;
    }
    return i;
}

/* return number of bytes necessary to represent i */
static int
byte_length(Py_ssize_t i)
{
    int n = 0;

    assert(i >= 0);
    while (i) {
        i >>= 8;
        n++;
    }
    return n;
}

/* ---------------------- sparse compressed bitarray -------------------
 *
 * see also: doc/sparse_compression.rst
 */

/* Bitarray buffer size (in bytes) that can be indexed by n bytes.  E.g.:
   with 1 byte you can index 256 bits which have a buffer size of 32 bytes.
   BSI(1) = 32, BSI(2) = 8_192, BSI(3) = 2_097_152, BSI(4) = 536_870_912 */
#define BSI(n)  (((Py_ssize_t) 1) << (8 * (n) - 3))

/* segment size in bytes - Although of little practical value, the code
   below will also work for SEGSIZE values of: 8, 16 and 32
   BSI(1) = 32 must be divisible by SEGSIZE.
   SEGSIZE must also be a multiple of the word size (sizeof(uint64_t) = 8).
   The size 32 is rooted in the fact that a bitarray of 32 bytes (256 bits)
   can be indexed with one index byte (BSI(1) = 32).  Our entire 'sc' format
   is constructed around this. */
#define SEGSIZE  32

/* number of segments for given number of bits */
#define NSEG(nbits)  (((nbits) + 8 * SEGSIZE - 1) / (8 * SEGSIZE))

/* Calculate an array with the running totals (rts) for 256 bit segments.
   Note that we call these "segments", as opposed to "blocks", in order to
   avoid confusion with encode blocks.

   0           1           2           3           4   index in rts array, i
   +-----------+-----------+-----------+-----------+
   |      5    |      0    |      3    |      4    |   segment population
   |           |           |           |           |
   |  [0:256]  | [256:512] | [512:768] | [768:987] |   bitarray slice
   +-----------+-----------+-----------+-----------+
   0           5           5           8          12   running totals, rts[i]

   In this example we have a bitarray of length nbits = 987.  Note that:

     * The number of segments is given by NSEG(nbits).
       Here we have 4 segments: NSEG(nbits) = NSEG(987) = 4

     * The rts array has always NSEG(nbits) + 1 elements, such that
       last element is always indexed by NSEG(nbits).

     * The element rts[0] is always zero.

     * The last element rts[NSEG(nbits)] is always the total count.
       Here: rts[NSEG(nbits)] = rts[NSEG(987)] = rts[4] = 12

     * The last segment may be partial.  In that case, it's size it given
       by nbits % 256.  Here: nbits % 256 = 987 % 256 = 219

   As each segment (at large) covers 256 bits (32 bytes), and each element
   in the running totals array takes up 8 bytes (on a 64-bit machine) the
   additional memory to accommodate the rts array is therefore 1/4 of the
   bitarray's memory.
   However, calculating this array upfront allows sc_count() to
   simply look up two entries from the array and take their difference.
   Thus, the speedup is significant.

   The function sc_write_indices() also takes advantage of the running
   totals array.  It loops over segments and skips to the next segment as
   soon as the count the current segment is reached.
*/
static Py_ssize_t *
sc_calc_rts(bitarrayobject *a)
{
    /* total number of segments - number of complete segments */
    const Py_ssize_t n_seg = NSEG(a->nbits);
    const Py_ssize_t c_seg = a->nbits / (8 * SEGSIZE);
    char zeros[SEGSIZE];                      /* segment with only zeros */
    Py_ssize_t cnt = 0;                       /* current count */
    char *buff;                               /* buffer in current segment */
    Py_ssize_t *res, m;

    memset(zeros, 0x00, SEGSIZE);
    res = (Py_ssize_t *) PyMem_Malloc((size_t)
                                      sizeof(Py_ssize_t) * (n_seg + 1));
    if (res == NULL)
        return (Py_ssize_t *) PyErr_NoMemory();

    for (m = 0; m < c_seg; m++) {  /* loop all complete segments */
        res[m] = cnt;
        buff = a->ob_item + m * SEGSIZE;
        assert((m + 1) * SEGSIZE <= Py_SIZE(a));

        if (memcmp(buff, zeros, SEGSIZE))  /* segment has not only zeros */
            cnt += popcnt_words((uint64_t *) buff, SEGSIZE / 8);
    }
    res[c_seg] = cnt;

    if (n_seg > c_seg) {           /* we have a final partial segment */
        cnt += count_from_word(a, c_seg * SEGSIZE / 8);
        res[n_seg] = cnt;
    }
    return res;
}

/* expose sc_calc_rts() to Python during debug mode for testing */
#ifndef NDEBUG
static PyObject *
sc_rts(PyObject *module, PyObject *obj)
{
    PyObject *list;
    bitarrayobject *a;
    Py_ssize_t *rts, i;

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    if ((rts = sc_calc_rts(a)) == NULL)
        return NULL;

    if ((list = PyList_New(NSEG(a->nbits) + 1)) == NULL)
        return NULL;

    for (i = 0; i <= NSEG(a->nbits); i++) {
        PyObject *item = PyLong_FromSsize_t(rts[i]);
        if (item == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, item);
    }
    PyMem_Free(rts);
    return list;
}
#endif  /* NDEBUG */


/* Equivalent to the Python expression:

      a.count(1, 8 * offset, 8 * offset + (1 << (8 * n)))

   The offset is required to be multiple of 32 (the segment size), as this
   functions makes use of running totals (stored in Py_ssize_t array rts). */
static Py_ssize_t
sc_count(bitarrayobject *a, Py_ssize_t *rts, Py_ssize_t offset, int n)
{
    Py_ssize_t nbits;

    assert(offset % SEGSIZE == 0 && n > 0);
    if (offset >= Py_SIZE(a))
        return 0;

    /* The desired number of bits to count up to (limited by remaining
       bitarray size) is given by:

           nbits = Py_MIN(8 * BSI(n), a->nbits - 8 * offset);

       However, on 32-bit machines this will fail for n=4 because 8 * BSI(4)
       is 1 << 32.  This is problematic, as 32-bit machines can address
       at least partially filled type 4 blocks).  Therefore, we first
       limit BSI(n) by the buffer size before multiplying 8. */
    nbits = Py_MIN(8 * Py_MIN(BSI(n), Py_SIZE(a)), a->nbits - 8 * offset);
    assert(nbits >= 0);

    offset /= SEGSIZE;               /* offset in terms of segments now */
    assert(NSEG(nbits) + offset <= NSEG(a->nbits));

    return rts[NSEG(nbits) + offset] - rts[offset];
}

/* Calculate number of bytes (1..128) of the raw block starting at offset,
   encode the block (write the header and copy the bytes into the encode
   buffer str), and return the number of raw bytes.
   Note that the encoded block size is the return value + 1. */
static int
sc_write_raw(char *str, bitarrayobject *a, Py_ssize_t *rts, Py_ssize_t offset)
{
    const Py_ssize_t nbytes = Py_SIZE(a) - offset;  /* remaining bytes */
    Py_ssize_t k = Py_MIN(32, nbytes);

    assert(nbytes > 0);
    if (k == 32) {
        /* We already know the first 32 bytes are better represented using
           raw bytes (otherwise this function wouldn't have been called).
           Now also check the next 3 segments. */
        while (k < 128 &&
               Py_MIN(32, nbytes - k) <= sc_count(a, rts, offset + k, 1))
            k += 32;

        assert(k % SEGSIZE == 0);
    }
    k = Py_MIN(k, nbytes);
    assert(0 < k && k <= 128 && k <= nbytes);

    /* block header */
    *str = (char) k;

    /* block data */
    assert(offset + k <= Py_SIZE(a));
    memcpy(str + 1, a->ob_item + offset, (size_t) k);

    return (int) k;
}

/* Write `k` indices (of `n` bytes each) into buffer `str`.
   Note that `n` (which is also the block type) has been selected
   (in sc_encode_block()) such that:

       k = sc_count(a, rts, offset, n) < 256
*/
static void
sc_write_indices(char *str, bitarrayobject *a, Py_ssize_t *rts,
                 Py_ssize_t offset, int n, int k)
{
    const char *str_stop = str + n * k;  /* stop position in buffer `str` */
    const char *buff = a->ob_item + offset;
    Py_ssize_t m, i;

    assert(1 <= n && n <= 4);
    assert(0 < k && k < 256);  /* note that k cannot be 0 in this function */
    assert(k == sc_count(a, rts, offset, n));   /* see above */
    assert(offset % SEGSIZE == 0);

    rts += offset / SEGSIZE;   /* rts index relative to offset now */

    for (m = 0;;) {  /* loop segments */
        int j, ni;

        assert(m + offset / SEGSIZE < NSEG(a->nbits));
        ni = (int) (rts[m + 1] - rts[m]);  /* indices in this segment */
        if (ni == 0)
            goto next_segment;

        for (i = m * SEGSIZE;; i++) {  /* loop bytes in segment */
            assert(i < (m + 1) * SEGSIZE && i + offset < Py_SIZE(a));
            if (buff[i] == 0)
                continue;

            for (j = 0; j < 8; j++)  /* loop bits */
                if (buff[i] & BITMASK(a, j)) {
                    write_n(str, n, 8 * i + j);
                    str += n;
                    if (--ni == 0) {
                        /* we have encountered all indices in this segment */
                        if (str == str_stop)
                            return;
                        goto next_segment;
                    }
                }
        }
    next_segment:
        m++;
    }
    Py_UNREACHABLE();
}

/* Write one sparse block (from `offset`, and up to `k` one bits).
   Return number of bytes written to buffer `str` (encoded block size). */
static Py_ssize_t
sc_write_sparse(char *str, bitarrayobject *a, Py_ssize_t *rts,
                Py_ssize_t offset, int n, int k)
{
    int len = 0;

    assert(1 <= n && n <= 4);
    assert(0 <= k && k < 256);

    /* write block header */
    if (n == 1) {               /* type 1 - single byte for each position */
        assert(k < 32);
        str[len++] = (char) (0xa0 + k);
    }
    else {                 /* type 2, 3, 4 - `n` bytes for each positions */
        str[len++] = (char) (0xc0 + n);
        str[len++] = (char) k;
    }
    if (k == 0)  /* no index bytes */
        return len;

    /* write block data - `k` indices, `n` bytes per index */
    sc_write_indices(str + len, a, rts, offset, n, k);
    return len + n * k;
}

/* Encode one block (starting at offset) and return offset increment.
   The output is written into str buffer and len is increased.

   Notes:

   - 32 index bytes take up as much space as a raw buffer of 32 bytes.
     Hence, we start by counting up to 32 bits in the first 32 bytes of the
     bitarray buffer in order to decide whether a raw block (type 0) should
     be used.

   - If a raw block is used, we check if the next three 32 byte buffer
     blocks are also suitable for raw encoding, see write_raw_block().
     Therefore, we have type 0 blocks with up to 128 raw bytes.

   - Now we decide which sparse block type to use.  We do this by
     first calculating the population count for the bitarray buffer size of
     the NEXT block type.  If the this count is larger than 255 (too large
     for the count byte) we have to stick with the current type.
     Otherwise we compare the encoded sizes of (a) sticking with the current
     type n, and (b) moving to the next type n+1.  These sizes are calculated
     as follows:

     (a) Although we consider sticking with the current type n, we are
         looking at the population for the next type block size.  We
         have to calculate the encoded size of ALL the type n blocks
         which would otherwise just be a single type n+1 block.  This
         size is:

             header size  *  number of blocks  +  n  *  population

     (b) Calculating the encoded size of a single block of type n+1 is
         much easier:

             header size  +  (n + 1)  *  population
 */
static Py_ssize_t
sc_encode_block(char *str, Py_ssize_t *len,
                bitarrayobject *a, Py_ssize_t *rts, Py_ssize_t offset)
{
    const Py_ssize_t nbytes = Py_SIZE(a) - offset;  /* remaining bytes */
    int count, n;

    assert(nbytes > 0);

    count = (int) sc_count(a, rts, offset, 1);
    /* are there fewer or equal raw bytes than index bytes */
    if (Py_MIN(32, nbytes) <= count) {           /* type 0 - raw bytes */
        int k = sc_write_raw(str + *len, a, rts, offset);
        *len += 1 + k;
        return k;
    }

    for (n = 1; n < 4; n++) {
        Py_ssize_t next_count, size_a, size_b;

        /* population for next block type n+1 */
        next_count = sc_count(a, rts, offset, n + 1);
        if (next_count >= 256)
            /* too many index bytes for next block type n+1 */
            break;

        /* encoded size of (up to 256) blocks of type n */
        size_a = ((n == 1 ? 1 : 2) * Py_MIN(256, (nbytes - 1) / BSI(n) + 1) +
                  n * next_count);
        /* encoded size of (a single) block of type n+1 */
        size_b = 2 + (n + 1) * next_count;

        if (size_a <= size_b)
            /* next block type n+1 is not smaller - use block type n */
            break;

        count = (int) next_count;
    }

    *len += sc_write_sparse(str + *len, a, rts, offset, n, count);
    return BSI(n);
}

static int
sc_encode_header(char *str, bitarrayobject *a)
{
    int len;

    len = byte_length(a->nbits);
    *str = (IS_BE(a) ? 0x10 : 0x00) | ((char) len);
    write_n(str + 1, len, a->nbits);

    return 1 + len;
}

static PyObject *
sc_encode(PyObject *module, PyObject *obj)
{
    PyObject *out;
    char *str;                  /* output buffer */
    Py_ssize_t len = 0;         /* bytes written into output buffer */
    bitarrayobject *a;
    Py_ssize_t offset = 0;      /* block offset into bitarray `a` in bytes */
    Py_ssize_t *rts;            /* running totals for 256 bit segments */

    if (ensure_bitarray(obj) < 0)
        return NULL;

    a = (bitarrayobject *) obj;
    set_padbits(a);
    if ((rts = sc_calc_rts(a)) == NULL)
        return NULL;

    out = PyBytes_FromStringAndSize(NULL, 32768);
    if (out == NULL)
        return NULL;

    str = PyBytes_AS_STRING(out);
    len += sc_encode_header(str, a);

    while (offset < Py_SIZE(a)) {
        Py_ssize_t allocated;   /* size (in bytes) of output buffer */

        /* Make sure we have enough space in output buffer for next block.
           The largest block possible is a type 4 block with 255 indices.
           It's size is: 2 header bytes + 4 * 255 index bytes */
        allocated = PyBytes_GET_SIZE(out);
        if (allocated < len + 2 + 4 * 255) {  /* increase allocation */
            if (_PyBytes_Resize(&out, allocated + 32768) < 0)
                return NULL;
            str = PyBytes_AS_STRING(out);
        }
        offset += sc_encode_block(str, &len, a, rts, offset);
    }
    PyMem_Free(rts);
    str[len++] = 0x00;          /* add stop byte */

    if (_PyBytes_Resize(&out, len) < 0)
        return NULL;

    return out;
}

PyDoc_STRVAR(sc_encode_doc,
"sc_encode(bitarray, /) -> bytes\n\
\n\
Compress a sparse bitarray and return its binary representation.\n\
This representation is useful for efficiently storing sparse bitarrays.\n\
Use `sc_decode()` for decompressing (decoding).");


static int
sc_decode_header(PyObject *iter, int *endian, Py_ssize_t *nbits)
{
    int head, len;

    if ((head = next_char(iter)) < 0)
        return -1;

    if (head & 0xe0) {
        PyErr_Format(PyExc_ValueError, "invalid header: 0x%02x", head);
        return -1;
    }

    *endian = head & 0x10 ? ENDIAN_BIG : ENDIAN_LITTLE;
    len = head & 0x0f;

    if (len > (int) sizeof(Py_ssize_t)) {
        PyErr_Format(PyExc_OverflowError,
                     "sizeof(Py_ssize_t) = %d: cannot read %d bytes",
                     (int) sizeof(Py_ssize_t), len);
        return -1;
    }
    if ((*nbits = read_n(len, iter)) < 0)
        return -1;

    return 0;
}

/* Read k bytes from iter and set elements in bitarray.  Return the size of
   the offset increment in bytes (i.e. just k), or -1 on failure. */
static Py_ssize_t
sc_read_raw(bitarrayobject *a, Py_ssize_t offset, PyObject *iter, int k)
{
    char *buff = a->ob_item + offset;
    Py_ssize_t i;

    if (offset + k > Py_SIZE(a)) {
        PyErr_Format(PyExc_ValueError, "decode error (raw): %zd + %d > %zd",
                     offset, k, Py_SIZE(a));
        return -1;
    }
    for (i = 0; i < k; i++) {
        int c;

        if ((c = next_char(iter)) < 0)
            return -1;
        buff[i] = (char) c;
    }
    return k;
}

/* Read n * k bytes from iter and set elements in bitarray.
   Return size of offset increment in bytes, or -1 on failure. */
static Py_ssize_t
sc_read_sparse(bitarrayobject *a, Py_ssize_t offset, PyObject *iter,
               int n, int k)
{
    assert(1 <= n && n <= 4 && k >= 0);
    while (k--) {
        Py_ssize_t i;

        if ((i = read_n(n, iter)) < 0)
            return -1;

        i += 8 * offset;
        /* also check for negative value as offset might cause overflow */
        if (i < 0 || i >= a->nbits) {
            PyErr_Format(PyExc_ValueError, "decode error (n=%d): %zd >= %zd",
                         n, i, a->nbits);
            return -1;
        }
        setbit(a, i, 1);
    }
    return BSI(n);
}

/* Decode one block: consume iter and set bitarray buffer at offset.
   Return size of offset increment in bytes, or -1 on failure. */
static Py_ssize_t
sc_decode_block(bitarrayobject *a, Py_ssize_t offset, PyObject *iter)
{
    int head;

    if ((head = next_char(iter)) < 0)
        return -1;

    if (head == 0)                         /* stop byte */
        return 0;

    if (head <= 0x80)                      /* type 0 - 0x01 .. 0x80 */
        return sc_read_raw(a, offset, iter, head);

    if (0xa0 <= head && head < 0xc0)       /* type 1 - 0xa0 .. 0xbf */
        return sc_read_sparse(a, offset, iter, 1, head - 0xa0);

    if (0xc2 <= head && head <= 0xc4) {    /* type 2 .. 4 - 0xc2 .. 0xc4 */
        int k;

        if ((k = next_char(iter)) < 0)
            return -1;

        return sc_read_sparse(a, offset, iter, head - 0xc0, k);
    }

    PyErr_Format(PyExc_ValueError, "invalid block head: 0x%02x", head);
    return -1;
}

static PyObject *
sc_decode(PyObject *module, PyObject *obj)
{
    PyObject *iter;
    bitarrayobject *a = NULL;
    Py_ssize_t offset = 0, increase, nbits;
    int endian;

    iter = PyObject_GetIter(obj);
    if (iter == NULL)
        return PyErr_Format(PyExc_TypeError, "'%s' object is not iterable",
                            Py_TYPE(obj)->tp_name);

    if (sc_decode_header(iter, &endian, &nbits) < 0)
        goto error;

    /* create bitarray of length 'nbits' and set all elements to 0 */
    a = new_bitarray(nbits, Py_None);
    if (a == NULL)
        goto error;
    a->endian = endian;
    memset(a->ob_item, 0x00, (size_t) Py_SIZE(a));

    /* consume blocks until stop byte is encountered */
    while ((increase = sc_decode_block(a, offset, iter))) {
        if (increase < 0)
            goto error;
        offset += increase;
    }
    Py_DECREF(iter);
    return (PyObject *) a;

 error:
    Py_DECREF(iter);
    Py_XDECREF((PyObject *) a);
    return NULL;
}

PyDoc_STRVAR(sc_decode_doc,
"sc_decode(stream) -> bitarray\n\
\n\
Decompress binary stream (an integer iterator, or bytes-like object) of a\n\
sparse compressed (`sc`) bitarray, and return the decoded  bitarray.\n\
This function consumes only one bitarray and leaves the remaining stream\n\
untouched.  Use `sc_encode()` for compressing (encoding).");

#undef BSI

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

/* Consume 'iter' while extending bitarray 'a'.
   Return 0 on success.  On failure, set exception and return -1. */
static int
vl_decode_core(bitarrayobject *a, PyObject *iter)
{
    Py_ssize_t padding;      /* number of pad bits read from header byte */
    Py_ssize_t i = 0;        /* bit counter */
    int k, c;

    if ((c = next_char(iter)) < 0)           /* header byte */
        return -1;

    padding = (c & 0x70) >> 4;
    if (padding >= 7 || ((c & 0x80) == 0 && padding > 4)) {
        PyErr_Format(PyExc_ValueError, "invalid header byte: 0x%02x", c);
        return -1;
    }
    for (k = 0; k < 4; k++)
        setbit(a, i++, (0x08 >> k) & c);

    while (c & 0x80) {
        if ((c = next_char(iter)) < 0)
            return -1;

        if (resize_lite(a, i + 7) < 0)
            return -1;
        assert(i + 6 < a->nbits);

        for (k = 0; k < 7; k++)
            setbit(a, i++, (0x40 >> k) & c);
    }
    /* set final length of bitarray */
    return resize_lite(a, i - padding);
}

static PyObject *
vl_decode(PyObject *module, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "endian", NULL};
    PyObject *obj, *iter, *endian = Py_None;
    bitarrayobject *a;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:vl_decode", kwlist,
                                     &obj, &endian))
        return NULL;

    iter = PyObject_GetIter(obj);
    if (iter == NULL)
        return PyErr_Format(PyExc_TypeError, "'%s' object is not iterable",
                            Py_TYPE(obj)->tp_name);

    a = new_bitarray(32, endian);
    if (a == NULL)
        goto error;

    if (vl_decode_core(a, iter) < 0)         /* do actual decoding work */
        goto error;

    Py_DECREF(iter);
    return (PyObject *) a;
 error:
    Py_DECREF(iter);
    Py_XDECREF((PyObject *) a);
    return NULL;
}

PyDoc_STRVAR(vl_decode_doc,
"vl_decode(stream, /, endian=None) -> bitarray\n\
\n\
Decode binary stream (an integer iterator, or bytes-like object), and\n\
return the decoded bitarray.  This function consumes only one bitarray and\n\
leaves the remaining stream untouched.  Use `vl_encode()` for encoding.");


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

    if (!PyArg_ParseTuple(args, "O!OO:canonical_decode",
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
#if IS_PY3K
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
    {"zeros",     (PyCFunction) zeros,     METH_KEYWORDS |
                                           METH_VARARGS, zeros_doc},
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
    {"deserialize",
                  (PyCFunction) deserialize,
                                           METH_O,       deserialize_doc},
    {"ba2hex",    (PyCFunction) ba2hex,    METH_O,       ba2hex_doc},
    {"hex2ba",    (PyCFunction) hex2ba,    METH_KEYWORDS |
                                           METH_VARARGS, hex2ba_doc},
    {"ba2base",   (PyCFunction) ba2base,   METH_VARARGS, ba2base_doc},
    {"base2ba",   (PyCFunction) base2ba,   METH_KEYWORDS |
                                           METH_VARARGS, base2ba_doc},
    {"sc_encode", (PyCFunction) sc_encode, METH_O,       sc_encode_doc},
    {"sc_decode", (PyCFunction) sc_decode, METH_O,       sc_decode_doc},
    {"vl_encode", (PyCFunction) vl_encode, METH_O,       vl_encode_doc},
    {"vl_decode", (PyCFunction) vl_decode, METH_KEYWORDS |
                                           METH_VARARGS, vl_decode_doc},
    {"canonical_decode",
                  (PyCFunction) chdi_new,  METH_VARARGS, chdi_doc},
#ifndef NDEBUG
    /* functionality exposed in debug mode for testing */
    {"_sc_rts",   (PyCFunction) sc_rts,    METH_O,       0},
#endif
    {NULL,        NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

#if IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_util", 0, -1, module_functions,
};
#endif

PyMODINIT_FUNC
#if IS_PY3K
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

#if IS_PY3K
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3("_util", module_functions, 0);
#endif
    if (m == NULL)
        goto error;

    if (PyType_Ready(&CHDI_Type) < 0)
        goto error;
    Py_SET_TYPE(&CHDI_Type, &PyType_Type);

#ifndef NDEBUG  /* expose segment size for testing */
    PyModule_AddObject(m, "_SEGSIZE", PyLong_FromSsize_t(SEGSIZE));
#endif

#if IS_PY3K
    return m;
 error:
    return NULL;
#else
 error:
    return;
#endif
}
