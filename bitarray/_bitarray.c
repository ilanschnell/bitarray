/*
   Copyright (c) 2008 - 2023, Ilan Schnell; All Rights Reserved
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

static int default_endian = ENDIAN_BIG;

static PyTypeObject Bitarray_Type;

/* translation table  - setup during module initialization */
static char reverse_trans[256];

#define bitarray_Check(obj)  PyObject_TypeCheck((obj), &Bitarray_Type)


static int
resize(bitarrayobject *self, Py_ssize_t nbits)
{
    const Py_ssize_t allocated = self->allocated, size = Py_SIZE(self);
    const Py_ssize_t newsize = BYTES(nbits);
    size_t new_allocated;

    if (self->ob_exports > 0) {
        PyErr_SetString(PyExc_BufferError,
                        "cannot resize bitarray that is exporting buffers");
        return -1;
    }

    if (self->buffer) {
        PyErr_SetString(PyExc_BufferError, "cannot resize imported buffer");
        return -1;
    }

    if (nbits < 0 || newsize < 0) {
        PyErr_Format(PyExc_OverflowError, "bitarray resize %zd", nbits);
        return -1;
    }

    assert(allocated >= size && size == BYTES(self->nbits));
    /* ob_item == NULL implies ob_size == allocated == 0 */
    assert(self->ob_item != NULL || (size == 0 && allocated == 0));
    /* allocated == 0 implies size == 0 */
    assert(allocated != 0 || size == 0);
    /* resize() is never called on readonly memory */
    assert(self->readonly == 0);

    if (newsize == size) {
        /* buffer size hasn't changed - bypass everything */
        self->nbits = nbits;
        return 0;
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
        PyMem_Free(self->ob_item);
        self->ob_item = NULL;
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        return 0;
    }

    /* Overallocate proportional to the bitarray size.
       Add padding to make the allocated size multiple of 4.
       The growth pattern is:  0, 4, 8, 16, 24, 32, 40, 48, 56, 64, 76, ...
       The pattern starts out the same as for lists but then grows at a
       smaller rate so that larger bitarrays only overallocate by 1/16th,
       as bitarrays are assumed to be memory critical. */
    new_allocated = ((size_t) newsize + (newsize >> 4) +
                     (newsize < 8 ? 3 : 7)) & ~(size_t) 3;

    /* Do not overallocate if the new size is closer to overallocated size
       than to the old size. */
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

/* create new bitarray object without initialization of buffer */
static PyObject *
newbitarrayobject(PyTypeObject *type, Py_ssize_t nbits, int endian)
{
    const Py_ssize_t nbytes = BYTES(nbits);
    bitarrayobject *obj;

    if (nbits < 0 || nbytes < 0)
        return PyErr_Format(PyExc_OverflowError, "new bitarray %zd", nbits);

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
    obj->buffer = NULL;
    obj->readonly = 0;
    return (PyObject *) obj;
}

static void
bitarray_dealloc(bitarrayobject *self)
{
    if (self->weakreflist)
        PyObject_ClearWeakRefs((PyObject *) self);

    if (self->buffer) {
        PyBuffer_Release(self->buffer);
        PyMem_Free(self->buffer);
    }
    else if (self->ob_item) {
        /* only free the object's buffer - imported buffers CANNOT be freed */
        PyMem_Free((void *) self->ob_item);
    }

    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* setup translation table, which maps each byte to it's reversed:
   reverse_trans = {0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, ..., 0xff} */
static void
setup_reverse_trans(void)
{
    int j, k;

    for (k = 0; k < 256; k++) {
        reverse_trans[k] = 0x00;
        for (j = 0; j < 8; j++)
            if (k & 128 >> j)
                reverse_trans[k] |= 1 << j;
    }
}

/* reverse each byte in byte-range(a, b) */
static void
bytereverse(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b)
{
    char *buff;

    assert(0 <= a && a <= Py_SIZE(self));
    assert(0 <= b && b <= Py_SIZE(self));

    for (buff = self->ob_item + a; a < b; a++, buff++)
        *buff = reverse_trans[(unsigned char) *buff];
}

/* Shift bits in byte-range(a, b) by n bits to right (using uint64 shifts
   when possible).
   The parameter (bebr = big endian byte reverse) is used to allow this
   function to call itself without calling bytereverse().  Elsewhere, ie.
   outside this function itself, it should always be called with bebr=1. */
static void
shift_r8(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b, int n, int bebr)
{
    const int m = 8 - n;
    unsigned char *ucbuff = (unsigned char *) self->ob_item;
    Py_ssize_t i;

    assert(0 <= n && n < 8);
    assert(0 <= a && a <= Py_SIZE(self));
    assert(0 <= b && b <= Py_SIZE(self));
    assert(self->readonly == 0);
    if (n == 0 || a >= b)
        return;

    /* as the big-endian representation has reversed bit order in each
       byte, we reverse each byte, and (re-) reverse again below */
    if (bebr && IS_BE(self))
        bytereverse(self, a, b);

    if (PY_LITTLE_ENDIAN && b >= a + 8) {
        const Py_ssize_t wa = (a + 7) / 8;  /* word range(wa, wb) */
        const Py_ssize_t wb = b / 8;
        const Py_ssize_t va = 8 * wa, vb = 8 * wb;

        assert(wa <= wb && b - vb < 8 && va - a < 8);
        assert(a <  vb && vb <= b);
        assert(a <= va && va <  b);

        shift_r8(self, vb, b, n, 0);
        if (b != vb)  /* add byte from word below */
            ucbuff[vb] |= ucbuff[vb - 1] >> m;

        for (i = wb - 1; i >= wa; i--) {
            assert_byte_in_range(self, 8 * i + 7);
            /* shift word - assumes machine has little endian byteorder */
            ((uint64_t *) ucbuff)[i] <<= n;
            if (i != wa)    /* add shifted byte from next lower word */
                ucbuff[8 * i] |= ucbuff[8 * i - 1] >> m;
        }
        if (a != va)  /* add byte from below */
            ucbuff[va] |= ucbuff[va - 1] >> m;

        shift_r8(self, a, va, n, 0);
    }
    else {
        for (i = b - 1; i >= a; i--) {
            ucbuff[i] <<= n;    /* shift byte (from highest to lowest) */
            if (i != a)      /* add shifted next lower byte */
                ucbuff[i] |= ucbuff[i - 1] >> m;
        }
    }

    if (bebr && IS_BE(self))  /* (re-) reverse bytes */
        bytereverse(self, a, b);
}

/* copy n bits from other (starting at b) onto self (starting at a),
   please find details about how this function works in copy_n.txt */
static void
copy_n(bitarrayobject *self, Py_ssize_t a,
       bitarrayobject *other, Py_ssize_t b, Py_ssize_t n)
{
    assert(0 <= n && n <= self->nbits && n <= other->nbits);
    assert(0 <= a && a <= self->nbits - n);
    assert(0 <= b && b <= other->nbits - n);
    assert(self->readonly == 0);
    if (n == 0 || (self == other && a == b))
        return;

    if (a % 8 == 0 && b % 8 == 0) {              /***** aligned case *****/
        Py_ssize_t p1 = a / 8;
        Py_ssize_t p2 = (a + n - 1) / 8;
        char *cp2 = self->ob_item + p2;
        char m2 = ones_table[IS_BE(self)][(a + n) % 8];
        char t2 = *cp2;

        assert(p1 + BYTES(n) == p2 + 1 && p1 <= p2);

        memmove(self->ob_item + p1, other->ob_item + b / 8, (size_t) BYTES(n));
        if (self->endian != other->endian)
            bytereverse(self, p1, p2 + 1);

        if (m2)  /* restore bits not to be copied */
            *cp2 = (*cp2 & m2) | (t2 & ~m2);
    }
    else if (n < 8) {                            /***** small n case *****/
        Py_ssize_t i;

        if (a <= b) {  /* loop forward (delete) */
            for (i = 0; i < n; i++)
                setbit(self, i + a, getbit(other, i + b));
        }
        else {         /* loop backwards (insert) */
            for (i = n - 1; i >= 0; i--)
                setbit(self, i + a, getbit(other, i + b));
        }
    }
    else {                                       /***** general case *****/
        Py_ssize_t p1 = a / 8;
        Py_ssize_t p2 = (a + n - 1) / 8;
        Py_ssize_t p3 = b / 8;
        int sa = a % 8;
        int sb = -(b % 8);
        char *cp1 = self->ob_item + p1;
        char *cp2 = self->ob_item + p2;
        char m1 = ones_table[IS_BE(self)][sa];
        char m2 = ones_table[IS_BE(self)][(a + n) % 8];
        char t1 = *cp1, t2 = *cp2, t3 = other->ob_item[p3];
        Py_ssize_t i;

        assert(n >= 8 && cp1 <= cp2);
        assert(a - sa == 8 * p1);   /* useful equations */
        assert(b + sb == 8 * p3);
        assert(a + n > 8 * p2);

        if (sa + sb < 0)
            sb += 8;
        copy_n(self, a - sa, other, b + sb, n - sb);  /* aligned copy */
        shift_r8(self, p1, p2 + 1, sa + sb, 1);       /* right shift */

        if (m1)                   /* restore bits at p1 */
            *cp1 = (*cp1 & ~m1) | (t1 & m1);

        if (m2 && sa + sb)        /* if shifted, restore bits at p2 */
            *cp2 = (*cp2 & m2) | (t2 & ~m2);

        for (i = 0; i < sb; i++)  /* copy first bits missed by copy_n() */
            setbit(self, i + a, t3 & BITMASK(other, i + b));
    }
}

/* starting at start, delete n bits from self */
static int
delete_n(bitarrayobject *self, Py_ssize_t start, Py_ssize_t n)
{
    const Py_ssize_t nbits = self->nbits;

    assert(0 <= start && start <= nbits);
    assert(0 <= n && n <= nbits - start);
    /* start == nbits implies n == 0 */
    assert(start != nbits || n == 0);

    copy_n(self, start, self, start + n, nbits - start - n);
    return resize(self, nbits - n);
}

/* starting at start, insert n (uninitialized) bits into self */
static int
insert_n(bitarrayobject *self, Py_ssize_t start, Py_ssize_t n)
{
    const Py_ssize_t nbits = self->nbits;

    assert(0 <= start && start <= nbits);
    assert(n >= 0);

    if (resize(self, nbits + n) < 0)
        return -1;
    copy_n(self, start + n, self, start, nbits - start);
    return 0;
}

static void
invert(bitarrayobject *self)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    const Py_ssize_t cwords = nbytes / 8;      /* complete 64-bit words */
    char *buff = self->ob_item;
    uint64_t *wbuff = WBUFF(self);
    Py_ssize_t i;

    assert_nbits(self);
    assert(self->readonly == 0);
    for (i = 0; i < cwords; i++)
        wbuff[i] = ~wbuff[i];
    for (i = 8 * cwords; i < nbytes; i++)
        buff[i] = ~buff[i];
}

/* repeat self m times (negative m is treated as 0) */
static int
repeat(bitarrayobject *self, Py_ssize_t m)
{
    Py_ssize_t q, k = self->nbits;

    assert(self->readonly == 0);
    if (k == 0 || m == 1)       /* nothing to do */
        return 0;

    if (m <= 0)                 /* clear */
        return resize(self, 0);

    assert(m > 1 && k > 0);
    if (k >= PY_SSIZE_T_MAX / m) {
        PyErr_Format(PyExc_OverflowError,
                     "cannot repeat bitarray (of size %zd) %zd times", k, m);
        return -1;
    }
    q = k * m;  /* number of resulting bits */
    if (resize(self, q) < 0)
        return -1;

    /* k (initially nbits): number of bits which have been copied so far */
    while (k <= q / 2) {        /* double copies */
        copy_n(self, k, self, 0, k);
        k *= 2;
    }
    assert(q / 2 < k && k <= q);

    copy_n(self, k, self, 0, q - k);  /* copy remaining bits */
    return 0;
}

/* set bits in range(a, b) in self to vi */
static void
setrange(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b, int vi)
{
    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(self->readonly == 0);

    if (b >= a + 8) {
        const Py_ssize_t byte_a = BYTES(a);  /* byte range(byte_a, byte_b) */
        const Py_ssize_t byte_b = b / 8;

        assert(a + 8 > 8 * byte_a && 8 * byte_b + 8 > b);

        setrange(self, a, 8 * byte_a, vi);
        memset(self->ob_item + byte_a, vi ? 0xff : 0x00,
               (size_t) (byte_b - byte_a));
        setrange(self, 8 * byte_b, b, vi);
    }
    else {
        while (a < b)
            setbit(self, a++, vi);
    }
}

/* return number of 1 bits in self[a:b] */
static Py_ssize_t
count(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t cnt = 0;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    if (n <= 0)
        return 0;

    if (n >= 64) {
        const Py_ssize_t wa = (a + 63) / 64;  /* word range(wa, ba) */
        const Py_ssize_t wb = b / 64;

        assert(wa <= wb && 64 * wa - a < 64 && b - 64 * wb < 64);

        cnt += count(self, a, 64 * wa);
        cnt += popcnt_words(WBUFF(self) + wa, wb - wa);
        cnt += count(self, 64 * wb, b);
    }
    else if (n >= 8) {
        const Py_ssize_t byte_a = BYTES(a);  /* byte range(byte_a, byte_b) */
        const Py_ssize_t byte_b = b / 8;

        assert(8 * byte_a - a < 8 && b - 8 * byte_b < 8);
        assert(byte_a <= byte_b && byte_b - byte_a < 8);

        cnt += count(self, a, 8 * byte_a);
        if (byte_b > byte_a) {
            uint64_t tmp = 0;
            /* copy bytes we want to count into tmp word */
            memcpy((char *) &tmp, self->ob_item + byte_a, byte_b - byte_a);
            cnt += popcnt_64(tmp);
        }
        cnt += count(self, 8 * byte_b, b);
    }
    else {
        while (a < b)
            cnt += getbit(self, a++);
    }
    return cnt;
}

/* return index of first occurrence of vi in self[a:b], -1 when not found */
static Py_ssize_t
find_bit(bitarrayobject *self, int vi, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t res, i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(0 <= vi && vi <= 1);
    if (n <= 0)
        return -1;

    /* When the search range is greater than 64 bits, we skip uint64 words.
       Note that we cannot check for n >= 64 here as the function could then
       go into an infinite recursive loop when a word is found. */
    if (n > 64) {
        const Py_ssize_t wa = (a + 63) / 64;  /* word range(wa, wb) */
        const Py_ssize_t wb = b / 64;
        const uint64_t *wbuff = WBUFF(self);
        const uint64_t w = vi ? 0 : ~0;

        if ((res = find_bit(self, vi, a, 64 * wa)) >= 0)
            return res;

        for (i = wa; i < wb; i++) {  /* skip uint64 words */
            assert_byte_in_range(self, 8 * i + 7);
            if (w ^ wbuff[i])
                return find_bit(self, vi, 64 * i, 64 * i + 64);
        }
        return find_bit(self, vi, 64 * wb, b);
    }

    /* For the same reason as above, we cannot check for n >= 8 here. */
    if (n > 8) {
        const Py_ssize_t byte_a = BYTES(a);  /* byte range(byte_a, byte_b) */
        const Py_ssize_t byte_b = b / 8;
        const char *buff = self->ob_item;
        const char c = vi ? 0 : ~0;

        if ((res = find_bit(self, vi, a, 8 * byte_a)) >= 0)
            return res;

        for (i = byte_a; i < byte_b; i++) {  /* skip bytes */
            assert_byte_in_range(self, i);
            if (c ^ buff[i])
                return find_bit(self, vi, 8 * i, 8 * i + 8);
        }
        return find_bit(self, vi, 8 * byte_b, b);
    }

    for (i = a; i < b; i++) {
        if (getbit(self, i) == vi)
            return i;
    }
    return -1;
}

/* Return first occurrence of (sub) bitarray xa (in self), such that xa is
   contained within self[start:stop], or -1 when xa is not found. */
static Py_ssize_t
find_sub(bitarrayobject *self, bitarrayobject *xa,
         Py_ssize_t start, Py_ssize_t stop)
{
    Py_ssize_t i;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (xa->nbits == 1)         /* faster for sparse bitarrays */
        return find_bit(self, getbit(xa, 0), start, stop);

    while (start <= stop - xa->nbits) {
        for (i = 0; i < xa->nbits; i++)
            if (getbit(self, start + i) != getbit(xa, i))
                goto next;

        return start;
    next:
        start++;
    }
    return -1;
}

/* Return first occurrence of either a bit or a (sub) bitarray (depending
   on the type of object x) contained within self[start:stop], or -1 when
   not found.  On Error, set exception and return -2. */
static Py_ssize_t
find_obj(bitarrayobject *self, PyObject *x, Py_ssize_t start, Py_ssize_t stop)
{
    if (PyIndex_Check(x)) {
        int vi;

        if (!conv_pybit(x, &vi))
            return -2;
        return find_bit(self, vi, start, stop);
    }

    if (bitarray_Check(x))
        return find_sub(self, (bitarrayobject *) x, start, stop);

    PyErr_Format(PyExc_TypeError, "bitarray or int expected, "
                 "not '%s'", Py_TYPE(x)->tp_name);
    return -2;
}

/* return 1 when buffers overlap, 0 otherwise */
static int
buffers_overlap(bitarrayobject *self, bitarrayobject *other)
{
    if (Py_SIZE(self) == 0 || Py_SIZE(other) == 0)
        return 0;

/* is pointer in buffer? */
#define PIB(a, ptr)  (a->ob_item <= ptr && ptr < a->ob_item + Py_SIZE(a))
    return PIB(self, other->ob_item) || PIB(other, self->ob_item);
#undef PIB
}

/* place self->nbits characters ('0', '1' corresponding to self) into str */
static void
setstr01(bitarrayobject *self, char *str)
{
    Py_ssize_t i;

    for (i = 0; i < self->nbits; i++)
        str[i] = getbit(self, i) + '0';
}

/* set item i in self to given value */
static int
set_item(bitarrayobject *self, Py_ssize_t i, PyObject *value)
{
    int vi;

    if (!conv_pybit(value, &vi))
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
    /* ignore resize() return value as we fail anyhow */
    resize(self, original_nbits);
    return -1;
}

static int
extend_sequence(bitarrayobject *self, PyObject *sequence)
{
    const Py_ssize_t original_nbits = self->nbits;
    PyObject *item;
    Py_ssize_t n, i;

    n = PySequence_Size(sequence);
    if (n < 0)
        return -1;

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
        case '_':
        case ' ':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            continue;
        default:
            PyErr_Format(PyExc_ValueError, "expected '0' or '1' "
                         "(or whitespace, or underscore), got '%c' (0x%02x)",
                         c, c);
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
#if IS_PY3K
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

/**************************************************************************
                     Implementation of bitarray methods
 **************************************************************************/

/*
   All methods which modify the buffer need to raise an exception when the
   buffer is read-only.  This is necessary because the buffer may be imported
   from another object which has a read-only buffer.

   We decided to do this check at the top level here, by adding the
   RAISE_IF_READONLY macro to all methods which modify the buffer.
   We could have done it at the low level (in setbit(), etc.), however as
   many of these functions have no return value we decided to do it here.

   The situation is different from how resize() raises an exception when
   called on an imported buffer.  There, it is easy to raise the exception
   in resize() itself, as there only one function which resizes the buffer,
   and this function (resize()) needs to report failures anyway.
*/

/* raise when buffer is readonly */
#define RAISE_IF_READONLY(self, ret_value)                                  \
    if (((bitarrayobject *) self)->readonly) {                              \
        PyErr_SetString(PyExc_TypeError, "cannot modify read-only memory"); \
        return ret_value;                                                   \
    }

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

    RAISE_IF_READONLY(self, NULL);

    if (!conv_pybit(value, &vi))
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
bitarray_bytereverse(bitarrayobject *self, PyObject *args)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    Py_ssize_t start = 0, stop = nbytes;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|nn:bytereverse", &start, &stop))
        return NULL;

    if (start < 0)
        start += nbytes;
    if (stop < 0)
        stop += nbytes;

    if (start < 0 || start > nbytes || stop < 0 || stop > nbytes) {
        PyErr_SetString(PyExc_IndexError, "byte index out of range");
        return NULL;
    }
    bytereverse(self, start, stop);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(bytereverse_doc,
"bytereverse(start=0, stop=<end of buffer>, /)\n\
\n\
For each byte in byte-range(start, stop) reverse the bit order in-place.\n\
The start and stop indices are given in terms of bytes (not bits).\n\
Also note that this method only changes the buffer; it does not change the\n\
endianness of the bitarray object.  Padbits are left unchanged such that\n\
two consecutive calls will always leave the bitarray unchanged.");


static PyObject *
bitarray_buffer_info(bitarrayobject *self)
{
    PyObject *res, *ptr;

    ptr = PyLong_FromVoidPtr((void *) self->ob_item);
    if (ptr == NULL)
        return NULL;

    res = Py_BuildValue("Onsnniii",
                        ptr,
                        Py_SIZE(self),
                        ENDIAN_STR(self->endian),
                        PADBITS(self),
                        self->allocated,
                        self->readonly,
                        self->buffer ? 1 : 0,
                        self->ob_exports);
    Py_DECREF(ptr);
    return res;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info() -> tuple\n\
\n\
Return a tuple containing:\n\
\n\
0. memory address of buffer\n\
1. buffer size (in bytes)\n\
2. bit endianness as a string\n\
3. number of pad bits\n\
4. allocated memory for the buffer (in bytes)\n\
5. memory is read-only\n\
6. buffer is imported\n\
7. number of buffer exports");


static PyObject *
bitarray_clear(bitarrayobject *self)
{
    RAISE_IF_READONLY(self, NULL);
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
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, step = 1;
    Py_ssize_t cnt = 0, slicelength;
    int vi = 1;

    if (!PyArg_ParseTuple(args, "|O&nnn:count",
                          conv_pybit, &vi, &start, &stop, &step))
        return NULL;
    if (step == 0) {
        PyErr_SetString(PyExc_ValueError, "count step cannot be zero");
        return NULL;
    }
    slicelength = adjust_indices(self->nbits, &start, &stop, step);
    adjust_step_positive(slicelength, &start, &stop, &step);

    if (step == 1) {
        cnt = count(self, start, stop);
    }
    else {
        Py_ssize_t i;

        for (i = start; i < stop; i += step)
            cnt += getbit(self, i);
    }
    return PyLong_FromSsize_t(vi ? cnt : slicelength - cnt);
}

PyDoc_STRVAR(count_doc,
"count(value=1, start=0, stop=<end of array>, step=1, /) -> int\n\
\n\
Count the number of occurrences of `value` in the bitarray.");


static PyObject *
bitarray_endian(bitarrayobject *self)
{
    return Py_BuildValue("s", ENDIAN_STR(self->endian));
}

PyDoc_STRVAR(endian_doc,
"endian() -> str\n\
\n\
Return the bit endianness of the bitarray as a string (`little` or `big`).");


static PyObject *
bitarray_extend(bitarrayobject *self, PyObject *obj)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_dispatch(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(iterable, /)\n\
\n\
Append all items from `iterable` to the end of the bitarray.\n\
If the iterable is a string, each `0` and `1` are appended as\n\
bits (ignoring whitespace and underscore).");


static PyObject *
bitarray_fill(bitarrayobject *self)
{
    long p;

    RAISE_IF_READONLY(self, NULL);
    p = set_padbits(self);
    /* there is no reason to call resize() - .fill() will not raise
       BufferError when buffer is imported or exported */
    self->nbits += p;

    assert(self->nbits == 8 * Py_SIZE(self));
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
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, pos;
    PyObject *x;

    if (!PyArg_ParseTuple(args, "O|nn", &x, &start, &stop))
        return NULL;

    adjust_indices(self->nbits, &start, &stop, 1);

    if ((pos = find_obj(self, x, start, stop)) == -2)
        return NULL;
    return PyLong_FromSsize_t(pos);
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
    PyObject *result;

    result = bitarray_find(self, args);
    if (result == NULL)
        return NULL;

    assert(PyLong_Check(result));
    if (PyLong_AsSsize_t(result) < 0) {
        Py_DECREF(result);
#if IS_PY3K
        PyErr_Format(PyExc_ValueError, "%A not in bitarray",
                     PyTuple_GET_ITEM(args, 0));
#else
        PyErr_SetString(PyExc_ValueError, "item not in bitarray");
#endif
        return NULL;
    }
    return result;
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
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "nO&:insert", &i, conv_pybit, &vi))
        return NULL;

    adjust_index(self->nbits, &i, 1);

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

    RAISE_IF_READONLY(self, NULL);
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
    self->ob_item[i / 8] ^= BITMASK(self, i);
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
    char *str;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }

    repr = PyBytes_FromStringAndSize(NULL, nbytes + 1);
    if (repr == NULL)
        goto error;

    str = PyBytes_AsString(repr);
    /* first byte contains the number of pad bits */
    *str = (char) set_padbits(self);
    /* remaining bytes contain buffer */
    memcpy(str + 1, self->ob_item, (size_t) nbytes);

    result = Py_BuildValue("O(Os)O", Py_TYPE(self),
                           repr, ENDIAN_STR(self->endian), dict);
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
    size_t strsize;
    char *str;

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

    strcpy(str, "bitarray('");  /* has length 10 */
    setstr01(self, str + 10);
    str[strsize - 2] = '\'';
    str[strsize - 1] = ')';     /* no terminating '\0' */

    result = Py_BuildValue("s#", str, (Py_ssize_t) strsize);
    PyMem_Free((void *) str);
    return result;
}


static PyObject *
bitarray_reverse(bitarrayobject *self)
{
    const Py_ssize_t nbits = self->nbits;
    Py_ssize_t i, j;

    RAISE_IF_READONLY(self, NULL);

    if (nbits < 16 && nbits != 8) {
        /* small bitarray - swapping individual bits is slightly faster */
        for (i = 0, j = nbits - 1; i < j; i++, j--) {
            int t = getbit(self, i);
            setbit(self, i, getbit(self, j));
            setbit(self, j, t);
        }
    }
    else {
        const Py_ssize_t nbytes = Py_SIZE(self);
        const Py_ssize_t p = PADBITS(self);  /* number of pad bits */
        char *buff = self->ob_item;

        /* Increase self->nbits to full buffer size.  The p pad bits will
           later be the leading p bits.  To remove those p leading bits, we
           must have p extra bits at the end of the bitarray. */
        self->nbits += p;
        assert(0 <= p && p < 8 && self->nbits == 8 * nbytes);

        /* reverse order of bytes */
        for (i = 0, j = nbytes - 1; i < j; i++, j--) {
            char t = buff[i];
            buff[i] = buff[j];
            buff[j] = t;
        }
        /* reverse order of bits within each byte */
        bytereverse(self, 0, nbytes);

        /* remove the p pad bits at the end of the original bitarray that
           are now the leading p bits */
        copy_n(self, 0, self, p, nbits);

        /* restore number of bits */
        self->nbits = nbits;
        assert_nbits(self);
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(reverse_doc,
"reverse()\n\
\n\
Reverse all bits in the array (in-place).");


/* given either an int (0 or 1) or a non-empty bitarray,
   return a bitarrayobject (with a new reference) */
static bitarrayobject *
searcharg(PyObject *x)
{
    bitarrayobject *xa;

    if (PyIndex_Check(x)) {
        int vi;

        if (!conv_pybit(x, &vi))
            return NULL;
        xa = (bitarrayobject *) newbitarrayobject(&Bitarray_Type, 1,
                                                  ENDIAN_LITTLE);
        if (xa == NULL)
            return NULL;
        setbit(xa, 0, vi);
        return xa;
    }
    if (bitarray_Check(x)) {
        xa = (bitarrayobject *) x;
        if (xa->nbits == 0) {
            PyErr_SetString(PyExc_ValueError,
                            "can't search for empty bitarray");
            return NULL;
        }
        Py_INCREF(xa);
        return xa;
    }
    PyErr_Format(PyExc_TypeError, "bitarray or int expected, not '%s'",
                 Py_TYPE(x)->tp_name);
    return NULL;
}


static PyObject *
bitarray_search(bitarrayobject *self, PyObject *args)
{
    PyObject *list = NULL, *item = NULL, *x;
    Py_ssize_t limit = PY_SSIZE_T_MAX, p = 0;
    bitarrayobject *xa;

    if (!PyArg_ParseTuple(args, "O|n:search", &x, &limit))
        return NULL;

    if ((xa = searcharg(x)) == NULL)
        return NULL;

    if ((list = PyList_New(0)) == NULL)
        goto error;

    while ((p = find_sub(self, xa, p, self->nbits)) >= 0) {
        if (PyList_Size(list) >= limit)
            break;
        item = PyLong_FromSsize_t(p++);
        if (item == NULL || PyList_Append(list, item) < 0)
            goto error;
        Py_DECREF(item);
    }
    Py_DECREF(xa);
    return list;

 error:
    Py_XDECREF(item);
    Py_XDECREF(list);
    Py_DECREF(xa);
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
bitarray_setall(bitarrayobject *self, PyObject *value)
{
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
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
    Py_ssize_t nbits = self->nbits, cnt1;
    int reverse = 0;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    cnt1 = count(self, 0, nbits);
    if (reverse) {
        setrange(self, 0, cnt1, 1);
        setrange(self, cnt1, nbits, 0);
    }
    else {
        Py_ssize_t cnt0 = nbits - cnt1;
        setrange(self, 0, cnt0, 0);
        setrange(self, cnt0, nbits, 1);
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
    Py_ssize_t i;

    list = PyList_New(self->nbits);
    if (list == NULL)
        return NULL;

    for (i = 0; i < self->nbits; i++) {
        PyObject *item = PyLong_FromLong(getbit(self, i));
        if (item == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyDoc_STRVAR(tolist_doc,
"tolist() -> list\n\
\n\
Return bitarray as list of integer items.\n\
`a.tolist()` is equal to `list(a)`.");


static PyObject *
bitarray_frombytes(bitarrayobject *self, PyObject *buffer)
{
    Py_buffer view;
    Py_ssize_t t, p;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    /* Before we extend the raw bytes with the new data, we need to store
       the current size and pad bits, as the bitarray size might not be
       a multiple of 8.  After extending, we remove the pad bits again.
    */
    t = self->nbits;          /* number of bits before extending */
    p = PADBITS(self);        /* number of pad bits */
    assert(0 <= p && p < 8 && t + p == 8 * Py_SIZE(self));

    if (resize(self, t + p + 8 * view.len) < 0)
        goto error;
    assert(self->nbits == 8 * Py_SIZE(self));

    memcpy(self->ob_item + (Py_SIZE(self) - view.len),
           (char *) view.buf, (size_t) view.len);

    if (delete_n(self, t, p) < 0)  /* remove pad bits */
        goto error;
    assert(self->nbits == t + 8 * view.len);

    PyBuffer_Release(&view);
    Py_RETURN_NONE;

 error:
    PyBuffer_Release(&view);
    return NULL;
}

PyDoc_STRVAR(frombytes_doc,
"frombytes(bytes, /)\n\
\n\
Extend the bitarray with raw bytes from a bytes-like object.\n\
Each added byte will add eight bits to the bitarray.");


static PyObject *
bitarray_tobytes(bitarrayobject *self)
{
    set_padbits(self);
    return PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
}

PyDoc_STRVAR(tobytes_doc,
"tobytes() -> bytes\n\
\n\
Return the bitarray buffer in bytes (pad bits are set to zero).");


static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *bytes, *f;
    Py_ssize_t nread = 0, nbytes = -1;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &nbytes))
        return NULL;

    if (nbytes < 0)  /* read till EOF */
        nbytes = PY_SSIZE_T_MAX;

    while (nread < nbytes) {
        PyObject *ret;         /* return object from frombytes call */
        Py_ssize_t nblock = Py_MIN(nbytes - nread, BLOCKSIZE);
        int not_enough_bytes;

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

        ret = bitarray_frombytes(self, bytes);
        Py_DECREF(bytes);
        if (ret == NULL)
            return NULL;
        Py_DECREF(ret);  /* drop frombytes result */

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
Extend bitarray with up to `n` bytes read from file object `f` (or any\n\
other binary stream what supports a `.read()` method, e.g. `io.BytesIO`).\n\
Each read byte will add eight bits to the bitarray.  When `n` is omitted or\n\
negative, all bytes until EOF are read.  When `n` is non-negative but\n\
exceeds the data available, `EOFError` is raised (but the available data\n\
is still read and appended).");


static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    Py_ssize_t offset;

    set_padbits(self);
    for (offset = 0; offset < nbytes; offset += BLOCKSIZE) {
        PyObject *ret;          /* return object from write call */
        Py_ssize_t size = Py_MIN(nbytes - offset, BLOCKSIZE);

        assert(size >= 0 && offset + size <= nbytes);
        /* basically: f.write(memoryview(self)[offset:offset + size] */
        ret = PyObject_CallMethod(f, "write", BYTES_SIZE_FMT,
                                  self->ob_item + offset, size);
        if (ret == NULL)
            return NULL;
        Py_DECREF(ret);  /* drop write result */
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(tofile_doc,
"tofile(f, /)\n\
\n\
Write the byte representation of the bitarray to the file object f.");


static PyObject *
bitarray_to01(bitarrayobject *self)
{
    PyObject *result;
    char *str;

    str = (char *) PyMem_Malloc((size_t) self->nbits);
    if (str == NULL)
        return PyErr_NoMemory();

    setstr01(self, str);
    result = Py_BuildValue("s#", str, self->nbits);
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(to01_doc,
"to01() -> str\n\
\n\
Return a string containing '0's and '1's, representing the bits in the\n\
bitarray.");


static PyObject *
bitarray_unpack(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"zero", "one", NULL};
    PyObject *res;
    char zero = 0x00, one = 0x01, *str;
    Py_ssize_t i;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    res = PyBytes_FromStringAndSize(NULL, self->nbits);
    if (res  == NULL)
        return NULL;

    str = PyBytes_AsString(res);
    for (i = 0; i < self->nbits; i++)
        str[i] = getbit(self, i) ? one : zero;
    return res;
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\x01') -> bytes\n\
\n\
Return bytes containing one character for each bit in the bitarray,\n\
using the specified mapping.");


static PyObject *
bitarray_pack(bitarrayobject *self, PyObject *buffer)
{
    const Py_ssize_t nbits = self->nbits;
    Py_buffer view;
    Py_ssize_t i;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    if (resize(self, nbits + view.len) < 0) {
        PyBuffer_Release(&view);
        return NULL;
    }
    for (i = 0; i < view.len; i++)
        setbit(self, nbits + i, ((char *) view.buf)[i]);

    PyBuffer_Release(&view);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(pack_doc,
"pack(bytes, /)\n\
\n\
Extend the bitarray from a bytes-like object, where each byte corresponds\n\
to a single bit.  The byte `b'\\x00'` maps to bit 0 and all other bytes\n\
map to bit 1.");


static PyObject *
bitarray_pop(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i = -1;
    long vi;

    RAISE_IF_READONLY(self, NULL);
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
    vi = getbit(self, i);
    if (delete_n(self, i, 1) < 0)
        return NULL;

    return PyLong_FromLong(vi);
}

PyDoc_STRVAR(pop_doc,
"pop(index=-1, /) -> item\n\
\n\
Remove and return item at `index` (default last).\n\
Raises `IndexError` if index is out of range.");


static PyObject *
bitarray_remove(bitarrayobject *self, PyObject *value)
{
    Py_ssize_t i;
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
        return NULL;

    i = find_bit(self, vi, 0, self->nbits);
    if (i < 0)
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
    if (self->buffer)
        res += sizeof(Py_buffer);
    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof_doc, "Return the size of bitarray object in bytes.");


/* private method - called only when frozenbitarray is initialized to
   disallow memoryviews to change the buffer */
static PyObject *
bitarray_freeze(bitarrayobject *self)
{
    if (self->buffer) {
        assert(self->buffer->readonly == self->readonly);
        if (self->readonly == 0) {
            PyErr_SetString(PyExc_TypeError, "cannot import writable buffer "
                            "into frozenbitarray");
            return NULL;
        }
    }
    set_padbits(self);
    self->readonly = 1;
    Py_RETURN_NONE;
}

/* ---------- functionality exposed in debug mode for testing ---------- */

#ifndef NDEBUG

static PyObject *
bitarray_shift_r8(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t a, b;
    int n;

    if (!PyArg_ParseTuple(args, "nni", &a, &b, &n))
        return NULL;

    shift_r8(self, a, b, n, 1);
    Py_RETURN_NONE;
}

static PyObject *
bitarray_copy_n(bitarrayobject *self, PyObject *args)
{
    PyObject *other;
    Py_ssize_t a, b, n;

    if (!PyArg_ParseTuple(args, "nO!nn", &a, &Bitarray_Type, &other, &b, &n))
        return NULL;

    copy_n(self, a, (bitarrayobject *) other, b, n);
    Py_RETURN_NONE;
}

static PyObject *
bitarray_overlap(bitarrayobject *self, PyObject *other)
{
    if (!bitarray_Check(other)) {
        PyErr_SetString(PyExc_TypeError, "bitarray expected");
        return NULL;
    }
    return PyBool_FromLong(buffers_overlap(self, (bitarrayobject *) other));
}

#endif  /* NDEBUG */

/* ---------------------- bitarray getset members ---------------------- */

static PyObject *
bitarray_get_nbytes(bitarrayobject *self, void *Py_UNUSED(ignored))
{
    return PyLong_FromSsize_t(Py_SIZE(self));
}

static PyObject *
bitarray_get_padbits(bitarrayobject *self, void *Py_UNUSED(ignored))
{
    return PyLong_FromSsize_t(PADBITS(self));
}

static PyObject *
bitarray_get_readonly(bitarrayobject *self, void *Py_UNUSED(ignored))
{
    return PyBool_FromLong(self->readonly);
}

static PyGetSetDef bitarray_getsets [] = {
    {"nbytes", (getter) bitarray_get_nbytes, NULL,
     PyDoc_STR("buffer size in bytes")},
    {"padbits", (getter) bitarray_get_padbits, NULL,
     PyDoc_STR("number of pad bits")},
    {"readonly", (getter) bitarray_get_readonly, NULL,
     PyDoc_STR("bool indicating whether buffer is read only")},
    {NULL, NULL, NULL, NULL}
};

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

    res = bitarray_copy(self);
    if (res == NULL)
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

    res = bitarray_copy(self);
    if (res == NULL)
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
    return PyLong_FromLong(getbit(self, i));
}

static int
bitarray_ass_item(bitarrayobject *self, Py_ssize_t i, PyObject *value)
{
    RAISE_IF_READONLY(self, -1);

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
    Py_ssize_t pos;

    if ((pos = find_obj(self, value, 0, self->nbits)) == -2)
        return -1;

    return pos >= 0;
}

static PyObject *
bitarray_inplace_concat(bitarrayobject *self, PyObject *other)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_dispatch(self, other) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
bitarray_inplace_repeat(bitarrayobject *self, Py_ssize_t n)
{
    RAISE_IF_READONLY(self, NULL);
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

/* return new bitarray with item in self, specified by slice */
static PyObject *
getslice(bitarrayobject *self, PyObject *slice)
{
    Py_ssize_t start, stop, step, slicelength;
    PyObject *res;

    assert(PySlice_Check(slice));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return NULL;

    res = newbitarrayobject(Py_TYPE(self), slicelength, self->endian);
    if (res == NULL)
        return NULL;

#define rr  ((bitarrayobject *) res)
    if (step == 1) {
        copy_n(rr, 0, self, start, slicelength);
    }
    else {
        Py_ssize_t i, j;

        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(rr, i, getbit(self, j));
    }
#undef rr
    return res;
}

static int
check_mask_length(bitarrayobject *self, bitarrayobject *mask)
{
    if (self->nbits != mask->nbits) {
        PyErr_Format(PyExc_IndexError, "bitarray length is %zd, but "
                     "mask has length %zd", self->nbits, mask->nbits);
        return -1;
    }
    return 0;
}

/* return a new bitarray with items from 'self' masked by bitarray 'mask' */
static PyObject *
getmasked(bitarrayobject *self, bitarrayobject *mask)
{
    PyObject *res;
    Py_ssize_t i, j, n;

    if (check_mask_length(self, mask) < 0)
        return NULL;

    n = count(mask, 0, mask->nbits);
    res = newbitarrayobject(Py_TYPE(self), n, self->endian);
    if (res == NULL)
        return NULL;

    for (i = j = 0; i < mask->nbits; i++) {
        if (getbit(mask, i))
            setbit((bitarrayobject *) res, j++, getbit(self, i));
    }
    assert(j == n);
    return res;
}

/* Return j-th item from sequence.  The item is considered an index into
   an array with given length, and is normalized a pythonic manner.
   On failure, an exception is set and -1 is returned. */
static Py_ssize_t
index_from_seq(PyObject *sequence, Py_ssize_t j, Py_ssize_t length)
{
    PyObject *item;
    Py_ssize_t i;

    if ((item = PySequence_GetItem(sequence, j)) == NULL)
        return -1;

    i = PyNumber_AsSsize_t(item, PyExc_IndexError);
    Py_DECREF(item);
    if (i == -1 && PyErr_Occurred())
        return -1;
    if (i < 0)
        i += length;
    if (i < 0 || i >= length) {
        PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
        return -1;
    }
    return i;
}

/* return a new bitarray with items from 'self' listed by
   sequence (of indices) 'seq' */
static PyObject *
getsequence(bitarrayobject *self, PyObject *seq)
{
    PyObject *res;
    Py_ssize_t i, j, n;

    n = PySequence_Size(seq);
    res = newbitarrayobject(Py_TYPE(self), n, self->endian);
    if (res == NULL)
        return NULL;

    for (j = 0; j < n; j++) {
        if ((i = index_from_seq(seq, j, self->nbits)) < 0) {
            Py_DECREF(res);
            return NULL;
        }
        setbit((bitarrayobject *) res, j, getbit(self, i));
    }
    return res;
}

static int
subscr_seq_check(PyObject *item)
{
    if (PyTuple_Check(item)) {
        PyErr_SetString(PyExc_TypeError, "multiple dimensions not supported");
        return -1;
    }
    if (PySequence_Check(item))
        return 0;

    PyErr_Format(PyExc_TypeError, "bitarray indices must be integers, "
                 "slices or sequences, not '%s'", Py_TYPE(item)->tp_name);
    return -1;
}

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

    if (PySlice_Check(item))
        return getslice(self, item);

    if (bitarray_Check(item))
        return getmasked(self, (bitarrayobject *) item);

    if (subscr_seq_check(item) < 0)
        return NULL;

    return getsequence(self, item);
}

/* The following functions, namely setslice_bitarray(), setslice_bool() and
   delslice(), are called from assign_slice(). */

/* set items in self, specified by slice, to other bitarray */
static int
setslice_bitarray(bitarrayobject *self, PyObject *slice,
                  bitarrayobject *other)
{
    Py_ssize_t start, stop, step, slicelength, increase;
    int other_copied = 0, res = -1;

    assert(PySlice_Check(slice));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;

    /* number of bits by which self has to be increased (decreased) */
    increase = other->nbits - slicelength;

    /* Make a copy of other, in case the buffers overlap.  This is obviously
       the case when self and other are the same object, but can also happen
       when the two bitarrays share memory. */
    if (buffers_overlap(self, other)) {
        other = (bitarrayobject *) bitarray_copy(other);
        if (other == NULL)
            return -1;
        other_copied = 1;
    }

    if (step == 1) {
        if (increase > 0) {        /* increase self */
            if (insert_n(self, start + slicelength, increase) < 0)
                goto error;
        }
        if (increase < 0) {        /* decrease self */
            if (delete_n(self, start + other->nbits, -increase) < 0)
                goto error;
        }
        /* copy the new values into self */
        copy_n(self, start, other, 0, other->nbits);
    }
    else {
        Py_ssize_t i, j;

        assert(step != 1);
        if (increase != 0) {
            PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                         "size %zd to extended slice of size %zd",
                         other->nbits, slicelength);
            goto error;
        }
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, getbit(other, i));
    }

    res = 0;
 error:
    if (other_copied)
        Py_DECREF(other);
    return res;
}

/* set items in self, specified by slice, to value */
static int
setslice_bool(bitarrayobject *self, PyObject *slice, PyObject *value)
{
    Py_ssize_t start, stop, step, slicelength;
    int vi;

    assert(PySlice_Check(slice) && PyIndex_Check(value));
    if (!conv_pybit(value, &vi))
        return -1;

    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;
    adjust_step_positive(slicelength, &start, &stop, &step);

    if (step == 1) {
        setrange(self, start, stop, vi);
    }
    else {
        const char *table = bitmask_table[IS_BE(self)];
        char *buff = self->ob_item;
        Py_ssize_t i;

        if (vi) {
            for (i = start; i < stop; i += step)
                buff[i >> 3] |= table[i & 7];
        }
        else {
            for (i = start; i < stop; i += step)
                buff[i >> 3] &= ~table[i & 7];
        }
    }
    return 0;
}

/* delete items in self, specified by slice */
static int
delslice(bitarrayobject *self, PyObject *slice)
{
    Py_ssize_t start, stop, step, slicelength;

    assert(PySlice_Check(slice));
    if (PySlice_GetIndicesEx(slice, self->nbits,
                             &start, &stop, &step, &slicelength) < 0)
        return -1;
    adjust_step_positive(slicelength, &start, &stop, &step);

    if (step > 1) {
        Py_ssize_t i, j;

        /* set items not to be removed (up to stop) */
        for (i = j = start; i < stop; i++) {
            if ((i - start) % step != 0)
                setbit(self, j++, getbit(self, i));
        }
    }
    return delete_n(self, stop - slicelength, slicelength);
}

/* assign slice of bitarray self to value */
static int
assign_slice(bitarrayobject *self, PyObject *slice, PyObject *value)
{
    if (value == NULL)
        return delslice(self, slice);

    if (bitarray_Check(value))
        return setslice_bitarray(self, slice, (bitarrayobject *) value);

    if (PyIndex_Check(value))
        return setslice_bool(self, slice, value);

    PyErr_Format(PyExc_TypeError,
                 "bitarray or int expected for slice assignment, not '%s'",
                 Py_TYPE(value)->tp_name);
    return -1;
}

/* delete items in self, specified by mask */
static int
delmask(bitarrayobject *self, bitarrayobject *mask)
{
    Py_ssize_t n = 0, i;

    if (check_mask_length(self, mask) < 0)
        return -1;

    for (i = 0; i < mask->nbits; i++) {
        if (getbit(mask, i) == 0)  /* set items we want to keep */
            setbit(self, n++, getbit(self, i));
    }
    assert(self == mask || n == mask->nbits - count(mask, 0, mask->nbits));

    return resize(self, n);
}

/* assign mask of bitarray self to value */
static int
assign_mask(bitarrayobject *self, bitarrayobject *mask, PyObject *value)
{
    if (value == NULL)
        return delmask(self, mask);

    PyErr_SetString(PyExc_NotImplementedError, "masked assignment "
                    "not implemented - use bitwise operations");
    return -1;
}

/* assign sequence (of indices) of bitarray self to Boolean value */
static int
setseq_bool(bitarrayobject *self, PyObject *seq, PyObject *value)
{
    Py_ssize_t n, i, j;
    int vi;

    if (!conv_pybit(value, &vi))
        return -1;

    n = PySequence_Size(seq);
    for (j = 0; j < n; j++) {
        if ((i = index_from_seq(seq, j, self->nbits)) < 0)
            return -1;
        setbit(self, i, vi);
    }
    return 0;
}

/* assign sequence (of indices) of bitarray self to bitarray */
static int
setseq_bitarray(bitarrayobject *self, PyObject *seq, bitarrayobject *other)
{
    Py_ssize_t n, i, j;
    int other_copied = 0, res = -1;

    n = PySequence_Size(seq);
    if (n != other->nbits) {
        PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                     "size %zd to bitarray of size %zd",
                     n, other->nbits);
        return -1;
    }
    /* Make a copy of other, in case the buffers overlap.  This is obviously
       the case when self and other are the same object, but can also happen
       when the two bitarrays share memory. */
    if (buffers_overlap(self, other)) {
        other = (bitarrayobject *) bitarray_copy(other);
        if (other == NULL)
            return -1;
        other_copied = 1;
    }

    for (j = 0; j < n; j++) {
        if ((i = index_from_seq(seq, j, self->nbits)) < 0)
            goto error;
        setbit(self, i, getbit(other, j));
    }
    res = 0;
 error:
    if (other_copied)
        Py_DECREF(other);
    return res;
}

/* delete items in self, specified by sequence of indices */
static int
delsequence(bitarrayobject *self, PyObject *seq)
{
    bitarrayobject *mask;  /* temporary bitarray masking items to remove */
    Py_ssize_t nseq, i, j;
    int res = -1;

    nseq = PySequence_Size(seq);
    if (nseq == 0)   /* shortcut - sequence is empty - nothing to delete */
        return 0;

    /* create mask bitarray - note that it's endianness is irrelevant */
    mask = (bitarrayobject *) newbitarrayobject(&Bitarray_Type, self->nbits,
                                                ENDIAN_LITTLE);
    if (mask == NULL)
        return -1;
    memset(mask->ob_item, 0x00, (size_t) Py_SIZE(mask));

    /* set indices from sequence in mask */
    for (j = 0; j < nseq; j++) {
        if ((i = index_from_seq(seq, j, self->nbits)) < 0)
            goto error;
        setbit(mask, i, 1);
    }
    res = delmask(self, mask);  /* do actual work here */
 error:
    Py_DECREF(mask);
    return res;
}

/* assign sequence (of indices) of bitarray self to value */
static int
assign_sequence(bitarrayobject *self, PyObject *seq, PyObject *value)
{
    if (value == NULL)
        return delsequence(self, seq);

    if (bitarray_Check(value))
        return setseq_bitarray(self, seq, (bitarrayobject *) value);

    if (PyIndex_Check(value))
        return setseq_bool(self, seq, value);

    PyErr_Format(PyExc_TypeError,
                 "bitarray or int expected for sequence assignment, not '%s'",
                 Py_TYPE(value)->tp_name);
    return -1;
}

static int
bitarray_ass_subscr(bitarrayobject *self, PyObject *item, PyObject *value)
{
    RAISE_IF_READONLY(self, -1);

    if (PyIndex_Check(item)) {
        Py_ssize_t i;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;
        if (i < 0)
            i += self->nbits;
        return bitarray_ass_item(self, i, value);
    }

    if (PySlice_Check(item))
        return assign_slice(self, item, value);

    if (bitarray_Check(item))
        return assign_mask(self, (bitarrayobject *) item, value);

    if (subscr_seq_check(item) < 0)
        return -1;

    return assign_sequence(self, item, value);
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

    result = bitarray_copy(self);
    if (result == NULL)
        return NULL;

    invert((bitarrayobject *) result);
    return result;
}

/* perform bitwise in-place operation */
static void
bitwise(bitarrayobject *self, bitarrayobject *other, const char oper)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    const Py_ssize_t cwords = nbytes / 8;      /* complete 64-bit words */
    Py_ssize_t i;
    char *buff_s = self->ob_item;
    char *buff_o = other->ob_item;
    uint64_t *wbuff_s = WBUFF(self);
    uint64_t *wbuff_o = WBUFF(other);

    assert(self->nbits == other->nbits);
    assert(self->endian == other->endian);
    assert_nbits(self);
    switch (oper) {
    case '&':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] &= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] &= buff_o[i];
        break;

    case '|':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] |= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] |= buff_o[i];
        break;

    case '^':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] ^= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] ^= buff_o[i];
        break;

    default:
        Py_UNREACHABLE();
    }
}

/* Return 0 if both a and b are bitarray objects with same length and
   endianness.  Otherwise, set exception and return -1. */
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

#define BITWISE_FUNC(name, inplace, ostr)              \
static PyObject *                                      \
bitarray_ ## name (PyObject *self, PyObject *other)    \
{                                                      \
    PyObject *res;                                     \
                                                       \
    if (bitwise_check(self, other, ostr) < 0)          \
        return NULL;                                   \
    if (inplace) {                                     \
        RAISE_IF_READONLY(self, NULL);                 \
        res = self;                                    \
        Py_INCREF(res);                                \
    }                                                  \
    else {                                             \
        res = bitarray_copy((bitarrayobject *) self);  \
        if (res == NULL)                               \
            return NULL;                               \
    }                                                  \
    bitwise((bitarrayobject *) res,                    \
            (bitarrayobject *) other, *ostr);          \
    return res;                                        \
}

BITWISE_FUNC(and,  0, "&")   /* bitarray_and */
BITWISE_FUNC(or,   0, "|")   /* bitarray_or  */
BITWISE_FUNC(xor,  0, "^")   /* bitarray_xor */
BITWISE_FUNC(iand, 1, "&=")  /* bitarray_iand */
BITWISE_FUNC(ior,  1, "|=")  /* bitarray_ior  */
BITWISE_FUNC(ixor, 1, "^=")  /* bitarray_ixor */


/* shift bitarray n positions to left (right=0) or right (right=1) */
static void
shift(bitarrayobject *self, Py_ssize_t n, int right)
{
    const Py_ssize_t nbits = self->nbits;

    if (n >= nbits) {
        memset(self->ob_item, 0x00, (size_t) Py_SIZE(self));
        return;
    }

    assert(0 <= n && n < nbits);
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
shift_check(PyObject *self, PyObject *other, const char *ostr)
{
    Py_ssize_t n;

    if (!bitarray_Check(self) || !PyIndex_Check(other)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(self)->tp_name, Py_TYPE(other)->tp_name);
        return -1;
    }
    n = PyNumber_AsSsize_t(other, PyExc_OverflowError);
    if (n == -1 && PyErr_Occurred())
        return -1;

    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return -1;
    }
    return n;
}

#define SHIFT_FUNC(name, inplace, ostr)                \
static PyObject *                                      \
bitarray_ ## name (PyObject *self, PyObject *other)    \
{                                                      \
    PyObject *res;                                     \
    Py_ssize_t n;                                      \
                                                       \
    if ((n = shift_check(self, other, ostr)) < 0)      \
        return NULL;                                   \
    if (inplace) {                                     \
        RAISE_IF_READONLY(self, NULL);                 \
        res = self;                                    \
        Py_INCREF(res);                                \
    }                                                  \
    else {                                             \
        res = bitarray_copy((bitarrayobject *) self);  \
        if (res == NULL)                               \
            return NULL;                               \
    }                                                  \
    shift((bitarrayobject *) res, n, *ostr == '>');    \
    return res;                                        \
}

SHIFT_FUNC(lshift,  0, "<<")  /* bitarray_lshift */
SHIFT_FUNC(rshift,  0, ">>")  /* bitarray_rshift */
SHIFT_FUNC(ilshift, 1, "<<=") /* bitarray_ilshift */
SHIFT_FUNC(irshift, 1, ">>=") /* bitarray_irshift */


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
        PyErr_Format(PyExc_TypeError, "dict expected, got '%s'",
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

    RAISE_IF_READONLY(self, NULL);
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
#if IS_PY3K
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
    if (PyErr_Occurred())       /* from PyIter_Next() */
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
    PyMem_Free((void *) nd);
}

/* insert symbol (mapping to ba) into the tree */
static int
binode_insert_symbol(binode *tree, bitarrayobject *ba, PyObject *symbol)
{
    binode *nd = tree, *prev;
    Py_ssize_t i;

    for (i = 0; i < ba->nbits; i++) {
        int k = getbit(ba, i);

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
#if IS_PY3K
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

/* Traverse using the branches corresponding to bits in ba, starting
   at *indexp.  Return the symbol at the leaf node, or NULL when the end
   of the bitarray has been reached.  On error, set the appropriate exception
   and also return NULL.
*/
static PyObject *
binode_traverse(binode *tree, bitarrayobject *ba, Py_ssize_t *indexp)
{
    binode *nd = tree;
    Py_ssize_t start = *indexp;

    while (*indexp < ba->nbits) {
        assert(nd);
        nd = nd->child[getbit(ba, *indexp)];
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
    int k;

    if (nd == NULL)
        return 0;

    if (nd->symbol) {
        assert(nd->child[0] == NULL && nd->child[1] == NULL);
        if (PyDict_SetItem(dict, nd->symbol, (PyObject *) prefix) < 0)
            return -1;
        return 0;
    }

    for (k = 0; k < 2; k++) {
        bitarrayobject *t;      /* prefix of the two child nodes */
        int ret;

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

/* return whether the node is complete - has both children,
   or is a symbol node */
static int
binode_complete(binode *nd)
{
    if (nd == NULL)
        return 0;

    if (nd->symbol) {
        /* symbol node cannot have children */
        assert(nd->child[0] == NULL && nd->child[1] == NULL);
        return 1;
    }

    return (binode_complete(nd->child[0]) &&
            binode_complete(nd->child[1]));
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
    decodetreeobject *obj;

    if (!PyArg_ParseTuple(args, "O:decodetree", &codedict))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = binode_make_tree(codedict);
    if (tree == NULL)
        return NULL;

    obj = (decodetreeobject *) type->tp_alloc(type, 0);
    if (obj == NULL) {
        binode_delete(tree);
        return NULL;
    }
    obj->tree = tree;

    return (PyObject *) obj;
}

static PyObject *
decodetree_todict(decodetreeobject *self)
{
    PyObject *dict, *prefix;

    dict = PyDict_New();
    if (dict == NULL)
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

PyDoc_STRVAR(todict_doc,
"todict() -> dict\n\
\n\
Return a dict mapping the symbols to bitarrays.  This dict is a\n\
reconstruction of the code dict the `decodetree` was created with.");


static PyObject *
decodetree_complete(decodetreeobject *self)
{
    return PyBool_FromLong(binode_complete(self->tree));
}

PyDoc_STRVAR(complete_doc,
"complete() -> bool\n\
\n\
Return whether the tree is complete.  That is, whether or not all\n\
nodes have both children (unless they are symbols nodes).");


static PyObject *
decodetree_nodes(decodetreeobject *self)
{
    return PyLong_FromSsize_t(binode_nodes(self->tree));
}

PyDoc_STRVAR(nodes_doc,
"nodes() -> int\n\
\n\
Return the number of nodes in the tree (internal and symbol nodes).");


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

/* These methods are mostly useful for debugging and testing.  We provide
   docstrings, but they are not mentioned in the documentation, and are not
   part of the API */
static PyMethodDef decodetree_methods[] = {
    {"complete",   (PyCFunction) decodetree_complete, METH_NOARGS,
     complete_doc},
    {"nodes",      (PyCFunction) decodetree_nodes,    METH_NOARGS,
     nodes_doc},
    {"todict",     (PyCFunction) decodetree_todict,   METH_NOARGS,
     todict_doc},
    {"__sizeof__", (PyCFunction) decodetree_sizeof,   METH_NOARGS, 0},
    {NULL,          NULL}  /* sentinel */
};

PyDoc_STRVAR(decodetree_doc,
"decodetree(code, /) -> decodetree\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
create a binary tree object to be passed to `.decode()` or `.iterdecode()`.");

static PyTypeObject DecodeTree_Type = {
#if IS_PY3K
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
    0,                                        /* tp_as_number */
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

/* return a binary tree from a decodetree or codedict */
static binode *
get_tree(PyObject *obj)
{
    binode *tree;

    if (DecodeTree_Check(obj)) {
        tree = ((decodetreeobject *) obj)->tree;
    }
    else {
        if (check_codedict(obj) < 0)
            return NULL;

        tree = binode_make_tree(obj);
        if (tree == NULL)
            return NULL;
    }
    return tree;
}

static PyObject *
bitarray_decode(bitarrayobject *self, PyObject *obj)
{
    binode *tree;
    PyObject *list, *symbol;
    Py_ssize_t index = 0;

    if ((tree = get_tree(obj)) == NULL)
        return NULL;

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

    if ((tree = get_tree(obj)) == NULL)
        return NULL;

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
    Py_VISIT(it->decodetree);
    return 0;
}

static PyTypeObject DecodeIter_Type = {
#if IS_PY3K
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

    if ((xa = searcharg(x)) == NULL)
        return NULL;

    it = PyObject_GC_New(searchiterobject, &SearchIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->bao = self;
    /* searcharg() returns a new reference, so no Py_INCREF here */
    it->xa = xa;
    it->p = 0;                  /* start search at position 0 */
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(itersearch_doc,
"itersearch(sub_bitarray, /) -> iterator\n\
\n\
Searches for the given sub_bitarray in self, and return an iterator over\n\
the start positions where sub_bitarray matches self.");

static PyObject *
searchiter_next(searchiterobject *it)
{
    Py_ssize_t p;

    p = find_sub(it->bao, it->xa, it->p, it->bao->nbits);
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
#if IS_PY3K
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
    {"bytereverse",  (PyCFunction) bitarray_bytereverse, METH_VARARGS,
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
    {"_freeze",      (PyCFunction) bitarray_freeze,      METH_NOARGS,  0},

#ifndef NDEBUG
    /* functionality exposed in debug mode for testing */
    {"_shift_r8",    (PyCFunction) bitarray_shift_r8,    METH_VARARGS, 0},
    {"_copy_n",      (PyCFunction) bitarray_copy_n,      METH_VARARGS, 0},
    {"_overlap",     (PyCFunction) bitarray_overlap,     METH_O,       0},
#endif

    {NULL,           NULL}  /* sentinel */
};

/* ------------------------ bitarray initialization -------------------- */

/* Given a string, return an integer representing the endianness.
   If the string is invalid, set exception and return -1. */
static int
endian_from_string(const char *string)
{
    assert(default_endian == ENDIAN_LITTLE || default_endian == ENDIAN_BIG);

    if (string == NULL)
        return default_endian;

    if (strcmp(string, "little") == 0)
        return ENDIAN_LITTLE;

    if (strcmp(string, "big") == 0)
        return ENDIAN_BIG;

    PyErr_Format(PyExc_ValueError, "bit endianness must be either "
                                   "'little' or 'big', not '%s'", string);
    return -1;
}

/* create a new bitarray object whose buffer is imported from another object
   which exposes the buffer protocol */
static PyObject *
newbitarray_from_buffer(PyTypeObject *type, PyObject *buffer, int endian)
{
    Py_buffer view;
    bitarrayobject *obj;

    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    obj = (bitarrayobject *) type->tp_alloc(type, 0);
    if (obj == NULL) {
        PyBuffer_Release(&view);
        return NULL;
    }

    Py_SET_SIZE(obj, view.len);
    obj->ob_item = (char *) view.buf;
    obj->allocated = 0;       /* no buffer allocated (in this object) */
    obj->nbits = 8 * view.len;
    obj->endian = endian;
    obj->ob_exports = 0;
    obj->weakreflist = NULL;
    obj->readonly = view.readonly;

    obj->buffer = (Py_buffer *) PyMem_Malloc(sizeof(Py_buffer));
    if (obj->buffer == NULL) {
        PyObject_Del(obj);
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }
    memcpy(obj->buffer, &view, sizeof(Py_buffer));

    return (PyObject *) obj;
}

static PyObject *
newbitarray_from_index(PyTypeObject *type, PyObject *index, int endian)
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

/* Return a new bitarray from pickle bytes (created by .__reduce__()).
   The head byte specifies the number of pad bits, the remaining bytes
   consist of the buffer itself.  As the bit-endianness must be known,
   we pass this function the actual argument endian_str (and not just
   endian, which would default to the default bit-endianness).  This way,
   we can raise an exception when the endian argument was not provided to
   bitarray().  Also, we only call this function with a non-empty PyBytes
   object.
 */
static PyObject *
newbitarray_from_pickle(PyTypeObject *type, PyObject *bytes, char *endian_str)
{
    PyObject *res;
    Py_ssize_t nbytes;
    char *data;
    unsigned char head;
    int endian;

    if (endian_str == NULL) {
        PyErr_SetString(PyExc_ValueError, "endianness missing for pickle");
        return NULL;
    }
    if ((endian = endian_from_string(endian_str)) < 0)
        /* cannot happen as we called check the string before */
        return NULL;

    assert(PyBytes_Check(bytes));
    nbytes = PyBytes_GET_SIZE(bytes);
    assert(nbytes > 0);            /* verified in bitarray_new() */
    data = PyBytes_AS_STRING(bytes);
    head = *data;
    assert((head & 0xf8) == 0);    /* verified in bitarray_new() */

    if (nbytes == 1 && head)
        return PyErr_Format(PyExc_ValueError,
                            "invalid pickle header byte: 0x%02x", head);

    res = newbitarrayobject(type,
                            8 * (nbytes - 1) - ((Py_ssize_t) head),
                            endian);
    if (res == NULL)
        return NULL;
    memcpy(((bitarrayobject *) res)->ob_item, data + 1, (size_t) nbytes - 1);
    return res;
}

static PyObject *
bitarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *result, *initial = Py_None, *buffer = Py_None;
    char *endian_str = NULL;
    int endian;
    static char *kwlist[] = {"", "endian", "buffer", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OzO:bitarray",
                                     kwlist, &initial, &endian_str, &buffer))
        return NULL;

    endian = endian_from_string(endian_str);
    if (endian < 0)
        return NULL;

    if (buffer != Py_None) {
        if (initial != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                            "buffer requires no initial argument");
            return NULL;
        }
        return newbitarray_from_buffer(type, buffer, endian);
    }

    /* no arg / None */
    if (initial == Py_None)
        return newbitarrayobject(type, 0, endian);

    /* bool */
    if (PyBool_Check(initial)) {
        PyErr_SetString(PyExc_TypeError, "cannot create bitarray from bool");
        return NULL;
    }

    /* index (a number) */
    if (PyIndex_Check(initial))
        return newbitarray_from_index(type, initial, endian);

    /* bytes (for pickling) - must have head byte (0x00 .. 0x07) */
    if (PyBytes_Check(initial) && PyBytes_GET_SIZE(initial) > 0) {
        char head = *PyBytes_AS_STRING(initial);
        if ((head & 0xf8) == 0)
            return newbitarray_from_pickle(type, initial, endian_str);
    }

    /* bitarray: use its endianness (when endian argument missing) */
    if (bitarray_Check(initial) && endian_str == NULL)
        endian = ((bitarrayobject *) initial)->endian;

    /* leave remaining type dispatch to extend method */
    result = newbitarrayobject(type, 0, endian);
    if (result == NULL)
        return NULL;
    if (extend_dispatch((bitarrayobject *) result, initial) < 0) {
        Py_DECREF(result);
        return NULL;
    }
    return result;
}

static int
ssize_richcompare(Py_ssize_t v, Py_ssize_t w, int op)
{
    switch (op) {
    case Py_LT: return v <  w;
    case Py_LE: return v <= w;
    case Py_EQ: return v == w;
    case Py_NE: return v != w;
    case Py_GT: return v >  w;
    case Py_GE: return v >= w;
    default: Py_UNREACHABLE();
    }
}

static PyObject *
richcompare(PyObject *v, PyObject *w, int op)
{
    Py_ssize_t i = 0, vs, ws, c;
    bitarrayobject *va, *wa;
    char *vb, *wb;

    if (!bitarray_Check(v) || !bitarray_Check(w)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    va = (bitarrayobject *) v;
    wa = (bitarrayobject *) w;
    vs = va->nbits;
    ws = wa->nbits;
    vb = va->ob_item;
    wb = wa->ob_item;
    if (op == Py_EQ || op == Py_NE) {
        /* shortcuts for EQ/NE */
        if (vs != ws) {
            /* if sizes differ, the bitarrays differ */
            return PyBool_FromLong(op == Py_NE);
        }
        else if (va->endian == wa->endian) {
            /* sizes and endianness are the same - use memcmp() */
            int cmp = memcmp(vb, wb, (size_t) vs / 8);

            if (cmp == 0 && vs % 8)  /* if equal, compare remaining bits */
                cmp = zlc(va) != zlc(wa);

            return PyBool_FromLong((cmp == 0) ^ (op == Py_NE));
        }
    }

    /* search for the first index where items are different */
    c = Py_MIN(vs, ws) / 8;  /* common buffer size */
    if (va->endian == wa->endian) {
        /* equal endianness - skip ahead by comparing bytes directly */
        while (i < c && vb[i] == wb[i])
            i++;
    }
    else {
        /* opposite endianness - compare with reversed byte */
        while (i < c && vb[i] == reverse_trans[(unsigned char) wb[i]])
            i++;
    }
    i *= 8;  /* i is now the bit index up to which we compared bytes */

    for (; i < vs && i < ws; i++) {
        int vi = getbit(va, i);
        int wi = getbit(wa, i);

        if (vi != wi)
            /* we have an item that differs */
            return PyBool_FromLong(ssize_richcompare(vi, wi, op));
    }

    /* no more items to compare -- compare sizes */
    return PyBool_FromLong(ssize_richcompare(vs, ws, op));
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
        vi = getbit(it->bao, it->index);
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
#if IS_PY3K
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

/******************** bitarray buffer export interface ********************/
/*
   Here we create bitarray_as_buffer for exporting bitarray buffers.
   Buffer imports, are NOT handled here, but in newbitarray_from_buffer().
*/

static int
bitarray_getbuffer(bitarrayobject *self, Py_buffer *view, int flags)
{
    int ret;

    if (view == NULL) {
        self->ob_exports++;
        return 0;
    }
    ret = PyBuffer_FillInfo(view,
                            (PyObject *) self,  /* exporter */
                            (void *) self->ob_item,
                            Py_SIZE(self),
                            self->readonly,
                            flags);
    if (ret >= 0)
        self->ob_exports++;

    return ret;
}

static void
bitarray_releasebuffer(bitarrayobject *self, Py_buffer *view)
{
    self->ob_exports--;
}

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
#endif  /* old buffer protocol */


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
"bitarray(initializer=0, /, endian='big', buffer=None) -> bitarray\n\
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
`iterable`: Create bitarray from iterable or sequence of integers 0 or 1.\n\
\n\
Optional keyword arguments:\n\
\n\
`endian`: Specifies the bit endianness of the created bitarray object.\n\
Allowed values are `big` and `little` (the default is `big`).\n\
The bit endianness effects the buffer representation of the bitarray.\n\
\n\
`buffer`: Any object which exposes a buffer.  When provided, `initializer`\n\
cannot be present (or has to be `None`).  The imported buffer may be\n\
readonly or writable, depending on the object type.");


static PyTypeObject Bitarray_Type = {
#if IS_PY3K
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
    &bitarray_as_number,                      /* tp_as_number */
    &bitarray_as_sequence,                    /* tp_as_sequence */
    &bitarray_as_mapping,                     /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    &bitarray_as_buffer,                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
#if PY_MAJOR_VERSION == 2
    | Py_TPFLAGS_HAVE_WEAKREFS
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
    bitarray_getsets,                         /* tp_getset */
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
    return Py_BuildValue("s", ENDIAN_STR(default_endian));
}

PyDoc_STRVAR(get_default_endian_doc,
"get_default_endian() -> str\n\
\n\
Return the default endianness for new bitarray objects being created.\n\
Unless `_set_default_endian('little')` was called, the default endianness\n\
is `big`.");


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
sysinfo(PyObject *module)
{
    return Py_BuildValue("iiiiiiii",
                         (int) sizeof(void *),
                         (int) sizeof(size_t),
                         (int) sizeof(bitarrayobject),
                         (int) sizeof(decodetreeobject),
                         (int) sizeof(binode),
#if (defined(__clang__) || defined(__GNUC__))
                         1,
#else
                         0,
#endif
#ifndef NDEBUG
                         1,
#else
                         0,
#endif
                         (int) PY_LITTLE_ENDIAN
                         );
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo() -> tuple\n\
\n\
Return tuple containing:\n\
\n\
0. sizeof(void *)\n\
1. sizeof(size_t)\n\
2. sizeof(bitarrayobject)\n\
3. sizeof(decodetreeobject)\n\
4. sizeof(binode)\n\
5. __clang__ or __GNUC__ defined\n\
6. NDEBUG not defined\n\
7. PY_LITTLE_ENDIAN");


static PyMethodDef module_functions[] = {
    {"get_default_endian",  (PyCFunction) get_default_endian, METH_NOARGS,
     get_default_endian_doc},
    {"_set_default_endian", (PyCFunction) set_default_endian, METH_VARARGS,
     set_default_endian_doc},
    {"_sysinfo",            (PyCFunction) sysinfo,            METH_NOARGS,
     sysinfo_doc},
    {NULL, NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

#if IS_PY3K
/* register bitarray as collections.abc.MutableSequence */
static int
register_abc(void)
{
    PyObject *abc_module, *mutablesequence, *res;

    if ((abc_module = PyImport_ImportModule("collections.abc")) == NULL)
        return -1;

    mutablesequence = PyObject_GetAttrString(abc_module, "MutableSequence");
    Py_DECREF(abc_module);
    if (mutablesequence == NULL)
        return -1;

    res = PyObject_CallMethod(mutablesequence, "register", "O",
                              (PyObject *) &Bitarray_Type);
    Py_DECREF(mutablesequence);
    if (res == NULL)
        return -1;

    Py_DECREF(res);
    return 0;
}

static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_bitarray", 0, -1, module_functions,
};
#endif

PyMODINIT_FUNC
#if IS_PY3K
PyInit__bitarray(void)
#else
init_bitarray(void)
#endif
{
    PyObject *m;

    setup_reverse_trans();

#if IS_PY3K
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

#if IS_PY3K
    if (register_abc() < 0)
        goto error;
#endif

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
#if IS_PY3K
    return m;
 error:
    return NULL;
#else
 error:
    return;
#endif
}
