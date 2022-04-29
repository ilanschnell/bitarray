/*
  Much of the code below is copied and/or derived from Mark Adler's Puff:

      https://github.com/madler/zlib/blob/master/contrib/puff

  This is Marks's copyright notice:

  Copyright (C) 2002-2013 Mark Adler, all rights reserved
  version 2.3, 21 Jan 2013

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "bitarray.h"


#define MAXBITS    15           /* maximum bits in a code */
#define MAXLCODES 286           /* maximum number of literal/length codes */
#define MAXDCODES  30           /* maximum number of distance codes */
#define MAXCODES (MAXLCODES+MAXDCODES)  /* maximum codes lengths to read */
#define FIXLCODES 288           /* number of fixed literal/length codes */
#define FIXDCODES  32           /* number of fixed distance codes */
#define FIXCODES (FIXLCODES+FIXDCODES)  /* fixed codes lengths */


/* input and output state */
typedef struct {
    PyObject_HEAD
    /* input */
    bitarrayobject *in;         /* bitarray we're decoding */
    Py_ssize_t incnt;           /* current index in bitarray */
    /* output */
    PyObject *out;              /* bytearray output buffer */
    Py_ssize_t outcnt;          /* bytes written to out so far */
} state_obj;

static PyTypeObject state_type;


static int
read_uint(state_obj *s, int numbits)
{
    long res = 0;
    int i;

    if (s->incnt + numbits > s->in->nbits)
        Py_FatalError("not enough bits in buffer");

    for (i = 0; i < numbits; i++)
        res |= (long) getbit(s->in, s->incnt++) << i;

    return (int) res;
}

struct huffman {
    short *count;               /* number of symbols of each length */
    short *symbol;              /* canonically ordered symbols */
};

static int
decode(state_obj *s, const struct huffman *h)
{
    Py_ssize_t nbits = s->in->nbits;
    int len;            /* current number of bits in code */
    int code;           /* len bits being decoded */
    int first;          /* first code of length len */
    int count;          /* number of codes of length len */
    int index;          /* index of first code of length len in symbol table */

    if (s->incnt >= nbits) {
        PyErr_SetString(PyExc_ValueError, "no more bits to decode");
        return -1;
    }

    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= getbit(s->in, s->incnt++);  /* get next bit */
        count = h->count[len];
        if (code - count < first)           /* if length len, return symbol */
            return h->symbol[index + (code - first)];
        index += count;                     /* else update for next length */
        first += count;
        first <<= 1;
        code <<= 1;

        if (s->incnt >= nbits && len != MAXBITS) {
            PyErr_SetString(PyExc_ValueError, "reached end of bitarray");
            return -1;
        }
    }
    PyErr_SetString(PyExc_ValueError, "ran out of codes");
    return -1;
}

/* add a byte to self->out */
static int
append_byte(state_obj *self, int byte)
{
    char *cp;

    if (byte < 0 || byte > 0xff) {
        PyErr_Format(PyExc_ValueError, "invalid byte: %d", byte);
        return -1;
    }
    if (PyByteArray_Resize(self->out, self->outcnt + 1) < 0) {
        PyErr_NoMemory();
        return -1;
    }
    cp = PyByteArray_AS_STRING(self->out) + self->outcnt;
    *cp = (char) byte;

    self->outcnt++;
    return 0;
}

/* copy 'len' bytes starting at 'dist' bytes ago in self->out */
static int
dist_len_copy(state_obj *self, int dist, int len)
{
    char *out;

    if (len < 0) {
        PyErr_SetString(PyExc_ValueError, "length cannot be negative");
        return -1;
    }
    if (dist <= 0) {
        PyErr_SetString(PyExc_ValueError, "distance cannot be negative or 0");
        return -1;
    }
    if (dist > self->outcnt) {
        PyErr_SetString(PyExc_ValueError, "distance too far back");
        return -1;
    }

    if (PyByteArray_Resize(self->out, self->outcnt + len) < 0) {
        PyErr_NoMemory();
        return -1;
    }

    out = PyByteArray_AS_STRING(self->out);
    while (len--) {
        out[self->outcnt] = out[self->outcnt - dist];
        self->outcnt++;
    }

    return 0;
}

/* Given the list of code lengths length[0..n-1] representing a canonical
   Huffman code for n symbols, construct the tables required to decode those
   codes. */
static int
construct(struct huffman *h, const short *length, int n)
{
    int symbol;         /* current symbol when stepping through length[] */
    int len;            /* current length when stepping through h->count[] */
    int left;           /* number of possible codes left of current length */
    short offs[MAXBITS+1];      /* offsets in symbol table for each length */

    /* count number of codes of each length */
    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        (h->count[length[symbol]])++;   /* assumes lengths are within bounds */
    if (h->count[0] == n)               /* no codes! */
        return 0;                       /* complete, but decode() will fail */

    /* check for an over-subscribed or incomplete set of lengths */
    left = 1;                           /* one possible code of zero length */
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;                     /* one more bit, double codes left */
        left -= h->count[len];          /* deduct count from possible codes */
        if (left < 0)
            return left;                /* over-subscribed--return negative */
    }                                   /* left > 0 means incomplete */

    /* generate offsets into symbol table for each length for sorting */
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];

    /*
     * put symbols in table sorted by length, by symbol order within each
     * length
     */
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = symbol;

    /* return zero for complete set, positive for incomplete set */
    return left;
}

/* decode literal/length and distance codes until an end-of-block code */
static int
codes(state_obj *s, const struct huffman *lencode,
                    const struct huffman *distcode)
{
    int symbol;         /* decoded symbol */
    static const short lens[29] = { /* size base for length codes 257..285 */
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = { /* extra bits for length codes 257..285 */
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = { /* offset base for distance codes 0..29 */
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
    static const short dext[30] = { /* extra bits for distance codes 0..29 */
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    /* decode literals and length/distance pairs */
    do {
        symbol = decode(s, lencode);
        if (symbol < 0)         /* error in decode() */
            return -1;

        if (symbol < 256) {             /* literal: symbol is the byte */
            /* write out the literal */
            if (append_byte(s, symbol) < 0)
                return -1;
        }
        else if (symbol > 256) {
            int len;                    /* length for copy */
            unsigned dist;              /* distance for copy */

            /* get and compute length */
            symbol -= 257;
            if (symbol >= 29) {
                PyErr_Format(PyExc_ValueError,
                             "invalid fixed code: %d", symbol);
                return -1;
            }
            len = lens[symbol] + read_uint(s, lext[symbol]);

            /* get and check distance */
            symbol = decode(s, distcode);
            if (symbol < 0)     /* error in decode() */
                return -1;
            dist = dists[symbol] + read_uint(s, dext[symbol]);

            /* copy length bytes from distance bytes back */
            if (dist_len_copy(s, dist, len) < 0)
                return -1;
        }
    } while (symbol != 256);            /* end of block symbol */

    /* done with a valid fixed or dynamic block */
    return 0;
}

/* set using the Python module function _set_bato() */
static PyObject *bitarray_type_obj = NULL;

/* ------------------------ State Python interface ------------------ */

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
        PyErr_SetString(PyExc_TypeError, "bitarray expected");
        return -1;
    }
    return 0;
}

/* create a new initialized canonical Huffman decode iterator object */
static PyObject *
state_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *in, *out;
    state_obj *obj;

    if (!PyArg_ParseTuple(args, "OO:State", &in, &out))
        return NULL;
    if (ensure_bitarray(in) < 0)
        return NULL;
    if (!PyByteArray_Check(out)) {
        PyErr_SetString(PyExc_TypeError, "bytearary expected");
        return NULL;
    }

    obj = (state_obj *) type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    Py_INCREF(in);
    obj->in = (bitarrayobject *) in;
    obj->incnt = 0;

    Py_INCREF(out);
    obj->out = out;
    obj->outcnt = PyByteArray_Size(out);

    return (PyObject *) obj;
}

/* append one byte to self->out */
static PyObject *
state_append_byte(state_obj *self, PyObject *obj)
{
    Py_ssize_t byte;

    byte = PyNumber_AsSsize_t(obj, NULL);
    if (byte == -1 && PyErr_Occurred())
        return NULL;

    if (append_byte(self, byte) < 0)
        return NULL;

    Py_RETURN_NONE;
}

/* extend self->out with n bytes from self->in */
static PyObject *
state_extend_block(state_obj *self, PyObject *value)
{
    Py_ssize_t nbytes;

    nbytes = PyNumber_AsSsize_t(value, NULL);
    if (nbytes == -1 && PyErr_Occurred())
        return NULL;
    if (nbytes < 0 || nbytes > 0xffff)
        return PyErr_Format(PyExc_ValueError, "invalid block size: %zd",
                            nbytes);
    if (self->incnt % 8 != 0) {
        PyErr_SetString(PyExc_ValueError, "bits not aligned");
        return NULL;
    }
    if (self->incnt + 8 * nbytes > self->in->nbits) {
        PyErr_SetString(PyExc_ValueError, "not enough input");
        return NULL;
    }
    if (PyByteArray_Resize(self->out, self->outcnt + nbytes) < 0)
        return PyErr_NoMemory();

    memcpy(PyByteArray_AS_STRING(self->out) + self->outcnt,
           self->in->ob_item + self->incnt / 8,
           (size_t) nbytes);

    self->incnt += 8 * nbytes;
    self->outcnt += nbytes;

    Py_RETURN_NONE;
}

/* set array[0..n-1] from the n items of the Python sequence */
static int
set_lengths(PyObject *sequence, Py_ssize_t n, short *array)
{
    Py_ssize_t i, len;

    if (!PySequence_Check(sequence)) {
        PyErr_SetString(PyExc_TypeError, "sequence expected");
        return -1;
    }
    if (PySequence_Size(sequence) != n) {
        PyErr_Format(PyExc_ValueError, "sequence of size %zd expected", n);
        return -1;
    }

    for (i = 0; i < n; i++) {
        PyObject *item = PySequence_GetItem(sequence, i);

        if (item == NULL)
            return -1;
        len = PyNumber_AsSsize_t(item, PyExc_OverflowError);
        Py_DECREF(item);
        if (len == -1 && PyErr_Occurred())
            return -1;
        array[i] = (short) len;
    }

    return 0;
}

#define CHECK_MAX(n, maxcodes)                                         \
    if (n < 0)                                                         \
        return PyErr_Format(PyExc_ValueError,                          \
              "size of length list cannot be negative: %zd", n);       \
    if (n > maxcodes)                                                  \
        return PyErr_Format(PyExc_ValueError,                          \
              "size of length list too large: %zd > %d", n, maxcodes)

/* given the liter/lengths and distance lengths as one big list,
   decode literal/length and distance codes until an end-of-block code */
static PyObject *
state_decode_block(state_obj *self, PyObject *args)
{
    PyObject *sequence;
    Py_ssize_t nlen, ndist;
    struct huffman lencode, distcode;   /* length and distance codes */
    short lengths[FIXCODES];            /* descriptor code lengths */
    short lencnt[MAXBITS+1], lensym[FIXLCODES];     /* lencode memory */
    short distcnt[MAXBITS+1], distsym[FIXDCODES];   /* distcode memory */
    int err;                            /* construct() return value */

    if (!PyArg_ParseTuple(args, "Onn:decode_block", &sequence, &nlen, &ndist))
        return NULL;

    /* check arguments and set values in lengths[0..nlen+ndist-1] */
    CHECK_MAX(nlen, FIXLCODES);
    CHECK_MAX(ndist, FIXDCODES);
    if (set_lengths(sequence, nlen + ndist, lengths) < 0)
        return NULL;

    /* build huffman table for literal/length codes */
    lencode.count = lencnt;
    lencode.symbol = lensym;
    err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) {
        PyErr_SetString(PyExc_ValueError, "incomplete literal/lengths code");
        return NULL;
    }

    /* build huffman table for distance codes */
    distcode.count = distcnt;
    distcode.symbol = distsym;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) {
        PyErr_SetString(PyExc_ValueError, "incomplete distance code");
        return NULL;
    }

    /* decode data until end-of-block code */
    if (codes(self, &lencode, &distcode) < 0)
        return NULL;

    Py_RETURN_NONE;
}

/* create a Python list from array[0..n-1] with n elements */
static PyObject *
list_from_shorts(const short *array, Py_ssize_t n)
{
    PyObject *list, *item;
    Py_ssize_t i;

    list = PyList_New(n);
    if (list == NULL)
        return NULL;

    for (i = 0; i < n; i++) {
        item = PyLong_FromLong((long) array[i]);
        if (item == NULL)
            return NULL;
        if (PyList_SetItem(list, i, item) < 0)
            return NULL;
    }
    return list;
}

/* given the code length code lengths (always 19 of them),
   decode the liter/lengths and distance lengths into one big list */
static PyObject *
state_decode_lengths(state_obj *self, PyObject *args)
{
    PyObject *sequence;
    Py_ssize_t ncode;     /* number of lengths in descriptor (nlen + ndist) */
    int index;                          /* index of lengths[] */
    int err;                            /* construct() return value */
    short lengths[MAXCODES];            /* descriptor code lengths */
    short cnt[MAXBITS+1], sym[19];      /* codelencode memory */
    struct huffman codelencode;     /* length and distance code length code */

    if (!PyArg_ParseTuple(args, "On:decode_lengths", &sequence, &ncode))
        return NULL;

    /* check arguments and set lengths[0..18] */
    if (set_lengths(sequence, 19, lengths) < 0)
        return NULL;
    CHECK_MAX(ncode, MAXCODES);

    /* build huffman table for code lengths codes (codelencode) */
    codelencode.count = cnt;
    codelencode.symbol = sym;
    err = construct(&codelencode, lengths, 19);
    if (err != 0) {
        PyErr_SetString(PyExc_ValueError, "require complete code");
        return NULL;
    }
    /* as the coding information from lengths[] is now in codelencode,
       we can now use lengths[] to write the decoded codelencode into */

    /* read length/literal and distance code length tables */
    index = 0;
    while (index < ncode) {
        int symbol;             /* decoded value */

        symbol = decode(self, &codelencode);
        if (symbol < 0) {
            PyErr_SetString(PyExc_ValueError, "invalid symbol");
            return NULL;
        }
        if (symbol < 16)                /* length in 0..15 */
            lengths[index++] = symbol;
        else {                          /* repeat instruction */
            int len = 0;  /* last length to repeat, assume repeating zeros */
            int n;                      /* time to repeat last length */

            if (symbol == 16) {         /* repeat last length 3..6 times */
                if (index == 0) {
                    PyErr_SetString(PyExc_ValueError, "no last length!");
                    return NULL;
                }
                len = lengths[index - 1];       /* last length */
                n = 3 + read_uint(self, 2);
            }
            else if (symbol == 17)      /* repeat zero 3..10 times */
                n = 3 + read_uint(self, 3);
            else                        /* == 18, repeat zero 11..138 times */
                n = 11 + read_uint(self, 7);

            if (index + n > ncode) {
                PyErr_SetString(PyExc_ValueError, "too many lengths!");
                return NULL;
            }
            while (n--)            /* repeat last or zero n times */
                lengths[index++] = len;
        }
    }

    /* check for end-of-block code -- there better be one! */
    if (lengths[256] == 0) {
        PyErr_SetString(PyExc_ValueError, "no end-of-block code!");
        return NULL;
    }

    return list_from_shorts(lengths, ncode);
}

/* copy 'len' bytes starting at 'dist' bytes ago in self->out,
   if the count 'len' exceeds the distance 'dist, then some of the output
   data will be a copy of data that was copied earlier in the process */
static PyObject *
state_copy(state_obj *self, PyObject *args)
{
    Py_ssize_t dist, len;

    if (!PyArg_ParseTuple(args, "nn:copy", &dist, &len))
        return NULL;

    if (dist_len_copy(self, dist, len) < 0)
        return NULL;

    Py_RETURN_NONE;
}

/* return the value of the bit input counter */
static PyObject *
state_get_incnt(state_obj *self)
{
    return PyLong_FromSsize_t(self->incnt);
}

/* read numbits from the bit input and return them as an integer */
static PyObject *
state_read_uint(state_obj *self, PyObject *obj)
{
    Py_ssize_t numbits, res = 0;
    int i;

    numbits = PyNumber_AsSsize_t(obj, NULL);
    if (numbits == -1 && PyErr_Occurred())
        return NULL;

    if (numbits < 0) {
        PyErr_SetString(PyExc_ValueError, "number of bits cannot be negative");
        return NULL;
    }
    if (self->incnt + numbits > self->in->nbits) {
        PyErr_SetString(PyExc_ValueError, "not enough bits in buffer");
        return NULL;
    }
    for (i = 0; i < numbits; i++)
        res |= (Py_ssize_t) getbit(self->in, self->incnt++) << i;

    return PyLong_FromSsize_t(res);
}

static PyMethodDef state_methods[] = {
    {"append_byte",    (PyCFunction) state_append_byte,    METH_O,       0},
    {"extend_block",   (PyCFunction) state_extend_block,   METH_O,       0},
    {"decode_block",   (PyCFunction) state_decode_block,   METH_VARARGS, 0},
    {"decode_lengths", (PyCFunction) state_decode_lengths, METH_VARARGS, 0},
    {"copy",           (PyCFunction) state_copy,           METH_VARARGS, 0},
    {"get_incnt",      (PyCFunction) state_get_incnt,      METH_NOARGS,  0},
    {"read_uint",      (PyCFunction) state_read_uint,      METH_O,       0},
    {NULL,           NULL}  /* sentinel */
};

static void
state_dealloc(state_obj *self)
{
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyTypeObject state_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "State",
    .tp_basicsize = sizeof(state_obj),
    .tp_dealloc = (destructor) state_dealloc,
    .tp_hash = PyObject_HashNotImplemented,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = state_methods,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = state_new,
    .tp_free = PyObject_Del,
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
    {"_set_bato", (PyCFunction) set_bato, METH_O, 0},
    {NULL,        NULL}  /* sentinel */
};

static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_puff", 0, -1, module_functions,
};

PyMODINIT_FUNC PyInit__puff(void)
{
    PyObject *m;

    m = PyModule_Create(&moduledef);
    if (m == NULL)
        return NULL;

    Py_SET_TYPE(&state_type, &PyType_Type);
    Py_INCREF((PyObject *) &state_type);
    PyModule_AddObject(m, "State", (PyObject *) &state_type);

    PyModule_AddObject(m, "MAXLCODES", PyLong_FromSsize_t(MAXLCODES));
    PyModule_AddObject(m, "MAXDCODES", PyLong_FromSsize_t(MAXDCODES));
    PyModule_AddObject(m, "FIXLCODES", PyLong_FromSsize_t(FIXLCODES));
    PyModule_AddObject(m, "FIXDCODES", PyLong_FromSsize_t(FIXDCODES));

    return m;
}
