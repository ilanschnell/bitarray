/*
   Copyright (c) 2008 - 2026, Ilan Schnell; All Rights Reserved
   bitarray is published under the PSF license.

   This file is the C part of the bitarray package.
   All functionality of the bitarray object is implemented here.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "structmember.h"
#include "bitarray.h"

/* size used when reading / writing blocks from files (in bytes) */
#define BLOCKSIZE  65536

/* translation table - setup during module initialization */
static char reverse_trans[256];

static PyTypeObject Bitarray_Type;

#define bitarray_Check(obj)  PyObject_TypeCheck((obj), &Bitarray_Type)


static size_t
new_allocation(size_t size, size_t allocated, size_t newsize)
{
    assert(allocated >= size);
    assert(newsize > 0);

    if (allocated >= newsize) {
        /* current buffer is large enough to host the requested size */
        if (newsize >= allocated / 2)
            return allocated;  /* minor downsize - keep current allocation */

        return newsize;  /* major downsize - shrink to exact size */
    }
    else {
          /* need to grow buffer */
          size_t new_alloc = newsize;
          /* overallocate when previous size isn't zero and when growth
             is moderate */
          if (size != 0 && newsize / 2 <= allocated) {
              /* overallocate proportional to the bitarray size and
                 add padding to make the allocated size multiple of 4 */
              new_alloc += (newsize >> 4) + ((newsize < 8) ? 3 : 7);
              new_alloc &= ~(size_t) 3;
          }
          return new_alloc;
    }
}

static int
resize(bitarrayobject *self, Py_ssize_t nbits)
{
    const size_t size = Py_SIZE(self);
    const size_t allocated = self->allocated;
    const size_t newsize = BYTES((size_t) nbits);
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

    if (nbits < 0) {
        PyErr_Format(PyExc_OverflowError, "bitarray resize %zd", nbits);
        return -1;
    }

    assert(allocated >= size && size == BYTES((size_t) self->nbits));
    /* ob_item == NULL implies ob_size == allocated == 0 */
    assert(self->ob_item != NULL || (size == 0 && allocated == 0));
    /* resize() is never called on read-only memory */
    assert(self->readonly == 0);

    /* bypass everything when buffer size hasn't changed */
    if (newsize == size) {
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

    new_allocated = new_allocation(size, allocated, newsize);

    if (new_allocated == allocated) {
        /* bypass reallocation */
        Py_SET_SIZE(self, newsize);
        self->nbits = nbits;
        return 0;
    }

    assert(new_allocated >= newsize);
    self->ob_item = PyMem_Realloc(self->ob_item, new_allocated);
    if (self->ob_item == NULL) {
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        PyErr_NoMemory();
        return -1;
    }
    Py_SET_SIZE(self, newsize);
    self->allocated = new_allocated;
    self->nbits = nbits;
    return 0;
}

/* create new bitarray object without initialization of buffer */
static bitarrayobject *
newbitarrayobject(PyTypeObject *type, Py_ssize_t nbits, int endian)
{
    const size_t nbytes = BYTES((size_t) nbits);
    bitarrayobject *obj;

    assert(nbits >= 0);

    obj = (bitarrayobject *) type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    if (nbytes == 0) {
        obj->ob_item = NULL;
    }
    else {
        /* allocate exact size */
        obj->ob_item = (char *) PyMem_Malloc(nbytes);
        if (obj->ob_item == NULL) {
            PyObject_Del(obj);
            PyErr_NoMemory();
            return NULL;
        }
    }
    Py_SET_SIZE(obj, nbytes);
    obj->allocated = nbytes;  /* no overallocation */
    obj->nbits = nbits;
    obj->endian = endian;
    obj->ob_exports = 0;
    obj->weakreflist = NULL;
    obj->buffer = NULL;
    obj->readonly = 0;
    return obj;
}

/* return new copy of bitarray object self */
static bitarrayobject *
bitarray_cp(bitarrayobject *self)
{
    bitarrayobject *res;

    res = newbitarrayobject(Py_TYPE(self), self->nbits, self->endian);
    if (res == NULL)
        return NULL;
    if (Py_SIZE(self))
        memcpy(res->ob_item, self->ob_item, (size_t) Py_SIZE(self));
    return res;
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
        /* only free object's buffer - imported buffers cannot be freed */
        assert(self->buffer == NULL);
        PyMem_Free((void *) self->ob_item);
    }

    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* return 1 when buffers overlap, 0 otherwise */
static int
buffers_overlap(bitarrayobject *self, bitarrayobject *other)
{
    if (Py_SIZE(self) == 0 || Py_SIZE(other) == 0)
        return 0;

/* is pointer ptr in buffer of bitarray a ? */
#define PIB(a, ptr)  (a->ob_item <= ptr && ptr < a->ob_item + Py_SIZE(a))
    return PIB(self, other->ob_item) || PIB(other, self->ob_item);
#undef PIB
}

/* reverse bits in first n characters of p */
static void
bytereverse(char *p, Py_ssize_t n)
{
    assert(n >= 0);
    while (n--) {
        *p = reverse_trans[(unsigned char) *p];
        p++;
    }
}

/* The following two functions operate on first n bytes in buffer.
   Within this region, they shift all bits by k positions to right,
   i.e. towards higher addresses.
   They operate on little-endian and big-endian bitarrays respectively.
   As we shift right, we need to start with the highest address and loop
   downwards such that lower bytes are still unaltered.
   See also devel/shift_r8.c
*/
static void
shift_r8le(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if HAVE_BUILTIN_BSWAP64 || PY_LITTLE_ENDIAN   /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] <<= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] >> (8 - k);
    }
    assert(w == 0 || ((uintptr_t) buff) % 4 == 0);
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if HAVE_BUILTIN_BSWAP64 && PY_BIG_ENDIAN
        *p = builtin_bswap64(*p);
        *p <<= k;
        *p = builtin_bswap64(*p);
#else
        *p <<= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - k);
    }
}

static void
shift_r8be(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if HAVE_BUILTIN_BSWAP64 || PY_BIG_ENDIAN      /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] >>= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] << (8 - k);
    }
    assert(w == 0 || ((uintptr_t) buff) % 4 == 0);
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if HAVE_BUILTIN_BSWAP64 && PY_LITTLE_ENDIAN
        *p = builtin_bswap64(*p);
        *p >>= k;
        *p = builtin_bswap64(*p);
#else
        *p >>= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] << (8 - k);
    }
}

/* shift bits in byte-range(a, b) by k bits to right */
static void
shift_r8(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b, int k)
{
    unsigned char *buff = (unsigned char *) self->ob_item + a;
    Py_ssize_t n = b - a;       /* number of bytes to be shifted */
    Py_ssize_t s = 0;           /* distance to next aligned pointer */

    assert(0 <= k && k < 8);
    assert(0 <= a && a <= Py_SIZE(self));
    assert(0 <= b && b <= Py_SIZE(self));
    assert(self->readonly == 0);
    if (k == 0 || n <= 0)
        return;

    if (n >= 8) {
        s = to_aligned((void *) buff);
        buff += s;  /* align pointer for casting to (uint64_t *) */
        n -= s;
    }

    if (IS_LE(self)) {          /* little endian */
        shift_r8le(buff, n, k);
        if (s) {
            buff[0] |= buff[-1] >> (8 - k);
            shift_r8le(buff - s, s, k);
        }
    }
    else {                      /* big endian */
        shift_r8be(buff, n, k);
        if (s) {
            buff[0] |= buff[-1] << (8 - k);
            shift_r8be(buff - s, s, k);
        }
    }
}

/* Copy n bits from other (starting at b) onto self (starting at a).
   Please see devel/copy_n.py for more details.

   Notes:
     - self and other may have opposite bit-endianness
     - other may equal self - copy a section of self onto itself
     - when other and self are distinct objects, their buffers
       may not overlap
*/
static void
copy_n(bitarrayobject *self, Py_ssize_t a,
       bitarrayobject *other, Py_ssize_t b, Py_ssize_t n)
{
    Py_ssize_t p3 = b / 8, i;
    int sa = a % 8, sb = -(b % 8);
    char t3 = 0;  /* silence uninitialized warning on some compilers */

    assert(0 <= n && n <= self->nbits && n <= other->nbits);
    assert(0 <= a && a <= self->nbits - n);
    assert(0 <= b && b <= other->nbits - n);
    assert(self == other || !buffers_overlap(self, other));
    assert(self->readonly == 0);
    if (n == 0 || (self == other && a == b))
        return;

    if (sa + sb < 0) {
        t3 = other->ob_item[p3++];
        sb += 8;
    }
    if (n > sb) {
        const Py_ssize_t p1 = a / 8, p2 = (a + n - 1) / 8, m = BYTES(n - sb);
        const char *table = ones_table[IS_BE(self)];
        char *cp1 = self->ob_item + p1, m1 = table[sa];
        char *cp2 = self->ob_item + p2, m2 = table[(a + n) % 8];
        char t1 = *cp1, t2 = *cp2;

        assert(p1 + m <= Py_SIZE(self) && p3 + m <= Py_SIZE(other));
        memmove(cp1, other->ob_item + p3, (size_t) m);
        if (self->endian != other->endian)
            bytereverse(cp1, m);

        shift_r8(self, p1, p2 + 1, sa + sb);
        if (m1)
            *cp1 = (*cp1 & ~m1) | (t1 & m1);     /* restore bits at p1 */
        if (m2)
            *cp2 = (*cp2 & m2) | (t2 & ~m2);     /* restore bits at p2 */
    }
    for (i = 0; i < sb && i < n; i++)            /* copy first sb bits */
        setbit(self, i + a, t3 & BITMASK(other, i + b));
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

    /* k: number of bits which have been copied so far */
    while (k <= q / 2) {        /* double copies */
        copy_n(self, k, self, 0, k);
        k *= 2;
    }
    assert(q / 2 < k && k <= q);

    copy_n(self, k, self, 0, q - k);  /* copy remaining bits */
    return 0;
}

/* the following functions xyz_span, xyz_range operate on bitarray items:
     - xyz_span: contiguous bits - self[a:b] (step=1)
     - xyz_range: self[start:stop:step]      (step > 0 is required)
 */

/* invert bits self[a:b] in-place */
static void
invert_span(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;  /* number of bits to invert */
    Py_ssize_t i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(self->readonly == 0);

    if (n >= 64) {
        const Py_ssize_t wa = (a + 63) / 64;  /* word-range(wa, wb) */
        const Py_ssize_t wb = b / 64;
        uint64_t *wbuff = WBUFF(self);

        invert_span(self, a, 64 * wa);
        for (i = wa; i < wb; i++)
            wbuff[i] = ~wbuff[i];
        invert_span(self, 64 * wb, b);
    }
    else if (n >= 8) {
        const Py_ssize_t ca = BYTES(a);       /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8;
        char *buff = self->ob_item;

        invert_span(self, a, 8 * ca);
        for (i = ca; i < cb; i++)
            buff[i] = ~buff[i];
        invert_span(self, 8 * cb, b);
    }
    else {                                    /* (bit-) range(a, b) */
        for (i = a; i < b; i++)
            self->ob_item[i / 8] ^= BITMASK(self, i);
    }
}

/* invert bits self[start:stop:step] in-place */
static void
invert_range(bitarrayobject *self,
             Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    assert(step > 0);

    if (step == 1) {
        invert_span(self, start, stop);
    }
    else {
        const char *table = bitmask_table[IS_BE(self)];
        char *buff = self->ob_item;
        Py_ssize_t i;

        for (i = start; i < stop; i += step)
            buff[i >> 3] ^= table[i & 7];
    }
}

/* set bits self[a:b] to vi */
static void
set_span(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b, int vi)
{
    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(self->readonly == 0);

    if (b >= a + 8) {
        const Py_ssize_t ca = BYTES(a);  /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8;

        assert(a + 8 > 8 * ca && 8 * cb + 8 > b);

        set_span(self, a, 8 * ca, vi);
        memset(self->ob_item + ca, vi ? 0xff : 0x00, (size_t) (cb - ca));
        set_span(self, 8 * cb, b, vi);
    }
    else {                               /* (bit-) range(a, b) */
        while (a < b)
            setbit(self, a++, vi);
    }
}

/* set bits self[start:stop:step] to vi */
static void
set_range(bitarrayobject *self,
          Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step, int vi)
{
    assert(step > 0);

    if (step == 1) {
        set_span(self, start, stop, vi);
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
}

/* return number of 1 bits in self[a:b] */
static Py_ssize_t
count_span(bitarrayobject *self, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t cnt = 0;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);

    if (n >= 64) {
        Py_ssize_t p = BYTES(a), w;  /* first full byte */
        p += to_aligned((void *) (self->ob_item + p));  /* align pointer */
        w = (b / 8 - p) / 8;         /* number of (full) words to count */

        assert(8 * p - a < 64 && b - (8 * (p + 8 * w)) < 64 && w >= 0);

        cnt += count_span(self, a, 8 * p);
        cnt += popcnt_words((uint64_t *) (self->ob_item + p), w);
        cnt += count_span(self, 8 * (p + 8 * w), b);
    }
    else if (n >= 8) {
        const Py_ssize_t ca = BYTES(a);   /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8, m = cb - ca;

        assert(8 * ca - a < 8 && b - 8 * cb < 8 && 0 <= m && m < 8);

        cnt += count_span(self, a, 8 * ca);
        if (m) {                /* starting at ca count in m bytes */
            uint64_t tmp = 0;
            /* copy bytes we want to count into tmp word */
            memcpy((char *) &tmp, self->ob_item + ca, (size_t) m);
            cnt += popcnt_64(tmp);
        }
        cnt += count_span(self, 8 * cb, b);
    }
    else {                                /* (bit-) range(a, b) */
        while (a < b)
            cnt += getbit(self, a++);
    }
    return cnt;
}

/* return number of 1 bits in self[start:stop:step] */
static Py_ssize_t
count_range(bitarrayobject *self,
            Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    assert(step > 0);

    if (step == 1) {
        return count_span(self, start, stop);
    }
    else {
        Py_ssize_t cnt = 0, i;

        for (i = start; i < stop; i += step)
            cnt += getbit(self, i);
        return cnt;
    }
}

/* return first (or rightmost in case right=1) occurrence
   of vi in self[a:b], -1 when not found */
static Py_ssize_t
find_bit(bitarrayobject *self, int vi, Py_ssize_t a, Py_ssize_t b, int right);

static Py_ssize_t
find_bit_words(bitarrayobject *self, int vi,
               Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t wa = (a + 63) / 64;  /* word-range(wa, wb) */
    const Py_ssize_t wb = b / 64;
    const uint64_t *wbuff = WBUFF(self);
    const uint64_t w = vi ? 0 : ~0;
    Py_ssize_t res, i;

    if (right) {
        if ((res = find_bit(self, vi, 64 * wb, b, 1)) >= 0)
            return res;

        for (i = wb - 1; i >= wa; i--) {  /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_bit(self, vi, 64 * i, 64 * i + 64, 1);
        }
        return find_bit(self, vi, a, 64 * wa, 1);
    }
    else {
        if ((res = find_bit(self, vi, a, 64 * wa, 0)) >= 0)
            return res;

        for (i = wa; i < wb; i++) {       /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_bit(self, vi, 64 * i, 64 * i + 64, 0);
        }
        return find_bit(self, vi, 64 * wb, b, 0);
    }
}

static Py_ssize_t
find_bit_bytes(bitarrayobject *self, int vi,
               Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t ca = BYTES(a);  /* char-range(ca, cb) */
    const Py_ssize_t cb = b / 8;
    const char *buff = self->ob_item;
    const char c = vi ? 0 : ~0;
    Py_ssize_t res, i;

    if (right) {
        if ((res = find_bit(self, vi, 8 * cb, b, 1)) >= 0)
            return res;

        for (i = cb - 1; i >= ca; i--) {  /* skip bytes */
            if (c ^ buff[i])
                return find_bit(self, vi, 8 * i, 8 * i + 8, 1);
        }
        return find_bit(self, vi, a, 8 * ca, 1);
    }
    else {
        if ((res = find_bit(self, vi, a, 8 * ca, 0)) >= 0)
            return res;

        for (i = ca; i < cb; i++) {       /* skip bytes */
            if (c ^ buff[i])
                return find_bit(self, vi, 8 * i, 8 * i + 8, 0);
        }
        return find_bit(self, vi, 8 * cb, b, 0);
    }
}

static Py_ssize_t
find_bit(bitarrayobject *self, int vi, Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(0 <= vi && vi <= 1);
    if (n <= 0)
        return -1;

    /* When the search range is greater than 64 bits, we skip uint64 words.
       Note that we cannot check for n >= 64 here as the function could then
       go into an infinite recursive loop when a word is found. */
    if (n > 64)
        return find_bit_words(self, vi, a, b, right);

    /* For the same reason as above, we cannot check for n >= 8 here. */
    if (n > 8)
        return find_bit_bytes(self, vi, a, b, right);

    /* finally, search for the desired bit by stepping one-by-one */
    for (i = right ? b - 1 : a; a <= i && i < b; i += right ? -1 : 1)
        if (getbit(self, i) == vi)
            return i;

    return -1;
}

/* Return first/rightmost occurrence of sub-bitarray (in self), such that
   sub is contained within self[start:stop], or -1 when sub is not found. */
static Py_ssize_t
find_sub(bitarrayobject *self, bitarrayobject *sub,
         Py_ssize_t start, Py_ssize_t stop, int right)
{
    const Py_ssize_t sbits = sub->nbits;
    const Py_ssize_t step = right ? -1 : 1;
    Py_ssize_t i, k;

    stop -= sbits - 1;
    i = right ? stop - 1 : start;

    while (start <= i && i < stop) {
        for (k = 0; k < sbits; k++)
            if (getbit(self, i + k) != getbit(sub, k))
                goto next;

        return i;
    next:
        i += step;
    }
    return -1;
}

/* return the number of non-overlapping occurrences of sub-bitarray within
   self[start:stop] */
static Py_ssize_t
count_sub(bitarrayobject *self, bitarrayobject *sub,
          Py_ssize_t start, Py_ssize_t stop)
{
    const Py_ssize_t sbits = sub->nbits;
    Py_ssize_t pos, cnt = 0;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (sbits == 0)
        return (start <= stop) ? stop - start + 1 : 0;

    while ((pos = find_sub(self, sub, start, stop, 0)) >= 0) {
        start = pos + sbits;
        cnt++;
    }
    return cnt;
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
    const Py_ssize_t nbits = self->nbits;
    PyObject *item;

    assert(PyIter_Check(iter));
    while ((item = PyIter_Next(iter))) {
        if (resize(self, self->nbits + 1) < 0 ||
            set_item(self, self->nbits - 1, item) < 0)
        {
            Py_DECREF(item);
            /* ignore resize() return value as we fail anyhow */
            resize(self, nbits);
            return -1;
        }
        Py_DECREF(item);
    }
    if (PyErr_Occurred())
        return -1;

    return 0;
}

static int
extend_sequence(bitarrayobject *self, PyObject *sequence)
{
    const Py_ssize_t nbits = self->nbits;
    Py_ssize_t n, i;

    if ((n = PySequence_Size(sequence)) < 0)
        return -1;

    if (resize(self, nbits + n) < 0)
        return -1;

    for (i = 0; i < n; i++) {
        PyObject *item = PySequence_GetItem(sequence, i);
        if (item == NULL || set_item(self, nbits + i, item) < 0) {
            Py_XDECREF(item);
            resize(self, nbits);
            return -1;
        }
        Py_DECREF(item);
    }
    return 0;
}

static int
extend_unicode01(bitarrayobject *self, PyObject *unicode)
{
    const Py_ssize_t nbits = self->nbits;
    const Py_ssize_t length = PyUnicode_GET_LENGTH(unicode);
    Py_ssize_t i = nbits, j;  /* i is the current write index in self */

    if (resize(self, nbits + length) < 0)
        return -1;

    for (j = 0; j < length; j++) {
        Py_UCS4 ch = PyUnicode_READ_CHAR(unicode, j);

        switch (ch) {
        case '0':
        case '1':
            setbit(self, i++, ch - '0');
            continue;
        case '_':
            continue;
        }
        if (Py_UNICODE_ISSPACE(ch))
            continue;

        PyErr_Format(PyExc_ValueError, "expected '0' or '1' (or whitespace "
                     "or underscore), got '%c' (0x%02x)", ch, ch);
        resize(self, nbits);  /* no bits added on error */
        return -1;
    }
    return resize(self, i);  /* in case we ignored characters */
}

static int
extend_dispatch(bitarrayobject *self, PyObject *obj)
{
    PyObject *iter;

    /* dispatch on type */
    if (bitarray_Check(obj))                              /* bitarray */
        return extend_bitarray(self, (bitarrayobject *) obj);

    if (PyUnicode_Check(obj))                       /* Unicode string */
        return extend_unicode01(self, obj);

    if (PySequence_Check(obj))                            /* sequence */
        return extend_sequence(self, obj);

    if (PyIter_Check(obj))                                    /* iter */
        return extend_iter(self, obj);

    /* finally, try to get the iterator of the object */
    if ((iter = PyObject_GetIter(obj))) {
        int res = extend_iter(self, iter);
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
    Py_ssize_t pos;

    Py_BEGIN_CRITICAL_SECTION(self);
    pos = find_bit(self, 0, 0, self->nbits, 0);
    Py_END_CRITICAL_SECTION();

    return PyBool_FromLong(pos == -1);
}

PyDoc_STRVAR(all_doc,
"all() -> bool\n\
\n\
Return `True` when all bits in bitarray are 1.\n\
`a.all()` is a faster version of `all(a)`.");


static PyObject *
bitarray_any(bitarrayobject *self)
{
    Py_ssize_t pos;

    Py_BEGIN_CRITICAL_SECTION(self);
    pos = find_bit(self, 1, 0, self->nbits, 0);
    Py_END_CRITICAL_SECTION();

    return PyBool_FromLong(pos >= 0);
}

PyDoc_STRVAR(any_doc,
"any() -> bool\n\
\n\
Return `True` when any bit in bitarray is 1.\n\
`a.any()` is a faster version of `any(a)`.");


static PyObject *
bitarray_append(bitarrayobject *self, PyObject *value)
{
    int ret, vi;

    RAISE_IF_READONLY(self, NULL);

    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = resize(self, self->nbits + 1);
    if (ret == 0)
        setbit(self, self->nbits - 1, vi);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(append_doc,
"append(item, /)\n\
\n\
Append `item` to the end of the bitarray.");


static PyObject *
bitarray_bytereverse(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|nn:bytereverse", &start, &stop))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (stop == PY_SSIZE_T_MAX)
        stop = Py_SIZE(self);

    if (PySlice_AdjustIndices(Py_SIZE(self), &start, &stop, 1) > 0)
        bytereverse(self->ob_item + start, stop - start);

    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(bytereverse_doc,
"bytereverse(start=0, stop=<end of buffer>, /)\n\
\n\
For each byte in byte-range(`start`, `stop`) reverse bits in-place.\n\
The start and stop indices are given in terms of bytes (not bits) and\n\
are interpreted like slice bounds and clipped to the buffer size.\n\
Also note that this method only changes the buffer; it does not change the\n\
bit-endianness of the bitarray object.  Pad bits are left unchanged such\n\
that two consecutive calls will always leave the bitarray unchanged.");


static PyObject *
bitarray_buffer_info(bitarrayobject *self)
{
    PyObject *info, *res, *args, *address, *readonly, *imported;

    info = bitarray_module_attr("BufferInfo");
    if (info == NULL)
        return NULL;

    address = PyLong_FromVoidPtr((void *) self->ob_item);
    readonly = PyBool_FromLong(self->readonly);
    imported = PyBool_FromLong(self->buffer ? 1 : 0);
    if (address == NULL || readonly == NULL || imported == NULL)
        goto error;

    args = Py_BuildValue("OnsnnOOi",
                         address,
                         Py_SIZE(self),
                         ENDIAN_STR(self->endian),
                         PADBITS(self),
                         self->allocated,
                         readonly,
                         imported,
                         self->ob_exports);
    if (args == NULL)
        goto error;

    Py_DECREF(address);
    Py_DECREF(readonly);
    Py_DECREF(imported);
    res = PyObject_CallObject(info, args);
    Py_DECREF(args);
    Py_DECREF(info);
    return res;

 error:
    Py_XDECREF(address);
    Py_XDECREF(readonly);
    Py_XDECREF(imported);
    Py_DECREF(info);
    return NULL;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info() -> BufferInfo\n\
\n\
Return named tuple with following fields:\n\
\n\
0. `address`: memory address of buffer\n\
1. `nbytes`: buffer size (in bytes)\n\
2. `endian`: bit-endianness as a string\n\
3. `padbits`: number of pad bits\n\
4. `alloc`: allocated memory for buffer (in bytes)\n\
5. `readonly`: memory is read-only (bool)\n\
6. `imported`: buffer is imported (bool)\n\
7. `exports`: number of buffer exports");


static PyObject *
bitarray_clear(bitarrayobject *self)
{
    int ret;

    RAISE_IF_READONLY(self, NULL);

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = resize(self, 0);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(clear_doc,
"clear()\n\
\n\
Remove all items from bitarray.");


/* Set readonly member to 1 if self is an instance of frozenbitarray.
   Return PyObject of self.  On error, set exception and return NULL. */
static PyObject *
freeze_if_frozen(bitarrayobject *self)
{
    PyObject *frozen;  /* frozenbitarray class object */
    int is_frozen;

    assert(self->ob_exports == 0 && self->buffer == NULL);

    if (Py_TYPE(self) == &Bitarray_Type)  /* shortcut for common case */
        return (PyObject *) self;

    frozen = bitarray_module_attr("frozenbitarray");
    if (frozen == NULL)
        return NULL;

    is_frozen = PyObject_IsInstance((PyObject *) self, frozen);
    Py_DECREF(frozen);
    if (is_frozen < 0)
        return NULL;

    if (is_frozen) {
        set_padbits(self);
        self->readonly = 1;
    }
    return (PyObject *) self;
}


static PyObject *
bitarray_copy(bitarrayobject *self)
{
    bitarrayobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = bitarray_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    return freeze_if_frozen(res);
}

PyDoc_STRVAR(copy_doc,
"copy() -> bitarray\n\
\n\
Return copy of bitarray (with same bit-endianness).");


static PyObject *
bitarray_count(bitarrayobject *self, PyObject *args)
{
    PyObject *sub = Py_None;
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, step = 1, slicelength;
    Py_ssize_t cnt = 0;

    if (!PyArg_ParseTuple(args, "|Onnn:count",
                          &sub , &start, &stop, &step))
        return NULL;

    if (step == 0) {
        PyErr_SetString(PyExc_ValueError, "step cannot be zero");
        return NULL;
    }

    if (PyIndex_Check(sub) || sub == Py_None) {
        int vi = 1;

        if (PyIndex_Check(sub) && !conv_pybit(sub, &vi))
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
        adjust_step_positive(slicelength, &start, &stop, &step);
        cnt = count_range(self, start, stop, step);
        Py_END_CRITICAL_SECTION();
        return PyLong_FromSsize_t(vi ? cnt : slicelength - cnt);
    }

    if (bitarray_Check(sub)) {   /* sub-bitarray count */
        if (step != 1) {
            PyErr_SetString(PyExc_ValueError,
                            "step must be 1 for sub-bitarray count");
            return NULL;
        }
        Py_BEGIN_CRITICAL_SECTION2(self, sub);
        if (start <= self->nbits) {
            PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
            cnt = count_sub(self, (bitarrayobject *) sub, start, stop);
        }
        Py_END_CRITICAL_SECTION2();
        return PyLong_FromSsize_t(cnt);
    }

    return PyErr_Format(PyExc_TypeError, "sub_bitarray must be bitarray or "
                        "int, not '%s'", Py_TYPE(sub)->tp_name);
}

PyDoc_STRVAR(count_doc,
"count(value=1, start=0, stop=<end>, step=1, /) -> int\n\
\n\
Number of occurrences of `value` bitarray within `[start:stop:step]`.\n\
Optional arguments `start`, `stop` and `step` are interpreted in\n\
slice notation, meaning `a.count(value, start, stop, step)` equals\n\
`a[start:stop:step].count(value)`.\n\
The `value` may also be a sub-bitarray.  In this case non-overlapping\n\
occurrences are counted within `[start:stop]` (`step` must be 1).");


/* Extend self without running arbitrary Python code while self is locked. */
static int
extend_thread_safe(bitarrayobject *self, PyObject *obj)
{
#ifdef Py_GIL_DISABLED
    int res1, res2;
    bitarrayobject *tmp;

    if (bitarray_Check(obj)) {
        Py_BEGIN_CRITICAL_SECTION2(self, obj);
        res1 = extend_bitarray(self, (bitarrayobject *) obj);
        Py_END_CRITICAL_SECTION2();
        return res1;
    }

    /* Build input into a temporary bitarray first, so arbitrary iteration
       and Python conversions happen outside self's critical section. */
    tmp = newbitarrayobject(&Bitarray_Type, 0, self->endian);
    if (tmp == NULL)
        return -1;
    /* Even on failure, tmp may contain successfully consumed prefix bits.
       Append them below to preserve partial-extension behavior. */
    res1 = extend_dispatch(tmp, obj);

    Py_BEGIN_CRITICAL_SECTION(self);
    /* Append accumulated bits to self in one protected operation. */
    res2 = extend_bitarray(self, tmp);
    Py_END_CRITICAL_SECTION();
    Py_DECREF(tmp);
    return (res1 < 0 || res2 < 0) ? -1 : 0;
#else
    return extend_dispatch(self, obj);
#endif
}

static PyObject *
bitarray_extend(bitarrayobject *self, PyObject *obj)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_thread_safe(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(iterable, /)\n\
\n\
Append items from iterable to the end of the bitarray.\n\
If `iterable` is a (Unicode) string, each `0` and `1` are appended as\n\
bits (ignoring whitespace and underscore).");


static PyObject *
bitarray_fill(bitarrayobject *self)
{
    Py_ssize_t p;

    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    p = PADBITS(self);  /* number of pad bits */
    set_padbits(self);
    /* there is no reason to call resize() - .fill() will not raise
       BufferError when buffer is imported or exported */
    self->nbits += p;
    Py_END_CRITICAL_SECTION();

    return PyLong_FromSsize_t(p);
}

PyDoc_STRVAR(fill_doc,
"fill() -> int\n\
\n\
Add zeros to the end of the bitarray, such that the length will be\n\
a multiple of 8, and return the number of bits added [0..7].");


static PyObject *
bitarray_find(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "", "", "right", NULL};
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, pos = -1;
    int right = 0;
    PyObject *sub;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|nni", kwlist,
                                     &sub, &start, &stop, &right))
        return NULL;

    if (PyIndex_Check(sub)) {
        int vi;

        if (!conv_pybit(sub, &vi))
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
        pos = find_bit(self, vi, start, stop, right);
        Py_END_CRITICAL_SECTION();
        return PyLong_FromSsize_t(pos);
    }

    if (bitarray_Check(sub)) {   /* find sub-bitarray */
        Py_BEGIN_CRITICAL_SECTION2(self, sub);
        if (start <= self->nbits) {
            PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
            pos = find_sub(self, (bitarrayobject *) sub, start, stop, right);
        }
        Py_END_CRITICAL_SECTION2();
        return PyLong_FromSsize_t(pos);
    }

    return PyErr_Format(PyExc_TypeError, "sub_bitarray must be bitarray or "
                        "int, not '%s'", Py_TYPE(sub)->tp_name);
}

PyDoc_STRVAR(find_doc,
"find(sub_bitarray, start=0, stop=<end>, /, right=False) -> int\n\
\n\
Return lowest (or rightmost when `right=True`) index where sub_bitarray\n\
is found, such that sub_bitarray is contained within `[start:stop]`.\n\
Return -1 when sub_bitarray is not found.");


static PyObject *
bitarray_index(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    PyObject *result;

    result = bitarray_find(self, args, kwds);
    if (result == NULL)
        return NULL;

    assert(PyLong_Check(result));
    if (PyLong_AsSsize_t(result) < 0) {
        Py_DECREF(result);
        return PyErr_Format(PyExc_ValueError, "%A not in bitarray",
                            PyTuple_GET_ITEM(args, 0));
    }
    return result;
}

PyDoc_STRVAR(index_doc,
"index(sub_bitarray, start=0, stop=<end>, /, right=False) -> int\n\
\n\
Return lowest (or rightmost when `right=True`) index where sub_bitarray\n\
is found, such that sub_bitarray is contained within `[start:stop]`.\n\
Raises `ValueError` when sub_bitarray is not present.");


static int
insert_lock_held(bitarrayobject *self, Py_ssize_t i, int vi)
{
    const Py_ssize_t n = self->nbits;

    if (i < 0) {
        i += n;
        if (i < 0)
            i = 0;
    }
    if (i > n)
        i = n;

    if (insert_n(self, i, 1) < 0)
        return -1;

    setbit(self, i, vi);
    return 0;
}

static PyObject *
bitarray_insert(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i;
    int vi, ret;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "nO&:insert", &i, conv_pybit, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = insert_lock_held(self, i, vi);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(insert_doc,
"insert(index, value, /)\n\
\n\
Insert `value` into bitarray before `index`.");


static PyObject *
bitarray_invert(bitarrayobject *self, PyObject *args)
{
    PyObject *arg = Py_None;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|O:invert", &arg))
        return NULL;

    if (PyIndex_Check(arg)) {
        Py_ssize_t i;
        int err = 0;

        i = PyNumber_AsSsize_t(arg, NULL);
        if (i == -1 && PyErr_Occurred())
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "index out of range");
            err = 1;
        }
        else {
            self->ob_item[i / 8] ^= BITMASK(self, i);
        }
        Py_END_CRITICAL_SECTION();
        if (err)
            return NULL;
    }
    else if (PySlice_Check(arg)) {
        Py_ssize_t start, stop, step, slicelength;

        if (PySlice_Unpack(arg, &start, &stop, &step) < 0)
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
        adjust_step_positive(slicelength, &start, &stop, &step);
        invert_range(self, start, stop, step);
        Py_END_CRITICAL_SECTION();
    }
    else if (arg == Py_None) {
        Py_BEGIN_CRITICAL_SECTION(self);
        invert_span(self, 0, self->nbits);
        Py_END_CRITICAL_SECTION();
    }
    else {
        return PyErr_Format(PyExc_TypeError,
                            "index expected, not '%s' object",
                            Py_TYPE(arg)->tp_name);
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(invert_doc,
"invert(index=<all bits>, /)\n\
\n\
Invert bits in-place.  When `index` is omitted, invert all bits.\n\
When `index` is an integer, invert the single bit at index.\n\
When `index` is a slice, invert the selected bits.");


static PyObject *
bitarray_reduce(bitarrayobject *self)
{
    PyObject *reconstructor;
    PyObject *dict, *bytes, *result;
    int padbits;

    reconstructor = bitarray_module_attr("_bitarray_reconstructor");
    if (reconstructor == NULL)
        return NULL;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }

    Py_BEGIN_CRITICAL_SECTION(self);
    set_padbits(self);
    padbits = (int) PADBITS(self);
    bytes = PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
    Py_END_CRITICAL_SECTION();

    if (bytes == NULL) {
        Py_DECREF(dict);
        Py_DECREF(reconstructor);
        return NULL;
    }

    result = Py_BuildValue("O(OOsii)O", reconstructor, Py_TYPE(self), bytes,
                           ENDIAN_STR(self->endian), padbits,
                           self->readonly, dict);
    Py_DECREF(dict);
    Py_DECREF(reconstructor);
    Py_DECREF(bytes);
    return result;
}

PyDoc_STRVAR(reduce_doc, "Internal. Used for pickling support.");


static PyObject *
bitarray_repr(bitarrayobject *self)
{
    PyObject *result;
    Py_ssize_t nbits, strsize, i;
    char *str;
    int err = 1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (nbits == 0)
        return PyUnicode_FromString("bitarray()");

    strsize = nbits + 12;  /* 12 is length of "bitarray('')" */
    str = PyMem_New(char, strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    strcpy(str, "bitarray('");  /* has length 10 */

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = 0; i < nbits; i++)
            str[i + 10] = getbit(self, i) + '0';
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        PyMem_Free((void *) str);
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during repr()");
        return NULL;
    }

    str[strsize - 2] = '\'';
    str[strsize - 1] = ')';
    /* we know the string length beforehand - not null-terminated */
    result = PyUnicode_FromStringAndSize(str, strsize);
    PyMem_Free((void *) str);
    return result;
}


static void
reverse_lock_held(bitarrayobject *self)
{
    Py_ssize_t p = PADBITS(self);  /* number of pad bits */
    char *buff = self->ob_item;

    /* Increase self->nbits to full buffer size.  The p pad bits will
       later be the leading p bits.  To remove those p leading bits, we
       must have p extra bits at the end of the bitarray. */
    self->nbits += p;

    /* reverse order of bytes */
    swap_bytes(buff, Py_SIZE(self));

    /* reverse order of bits within each byte */
    bytereverse(self->ob_item, Py_SIZE(self));

    /* Remove the p pad bits at the end of the original bitarray that
       are now the leading p bits.
       The reason why we don't just call delete_n(self, 0, p) here is that
       it calls resize(), and we want to allow reversing an imported
       writable buffer. */
    copy_n(self, 0, self, p, self->nbits - p);
    self->nbits -= p;
}

static PyObject *
bitarray_reverse(bitarrayobject *self)
{
    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    reverse_lock_held(self);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(reverse_doc,
"reverse()\n\
\n\
Reverse all bits in bitarray (in-place).");


static PyObject *
bitarray_setall(bitarrayobject *self, PyObject *value)
{
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->ob_item)
        memset(self->ob_item, vi ? 0xff : 0x00, (size_t) Py_SIZE(self));
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setall_doc,
"setall(value, /)\n\
\n\
Set all elements in bitarray to `value`.\n\
Note that `a.setall(value)` is equivalent to `a[:] = value`.");


static void
sort_lock_held(bitarrayobject *self, int reverse)
{
    Py_ssize_t nbits = self->nbits, cnt1;

    cnt1 = count_span(self, 0, nbits);
    if (reverse) {
        set_span(self, 0, cnt1, 1);
        set_span(self, cnt1, nbits, 0);
    }
    else {
        Py_ssize_t cnt0 = nbits - cnt1;
        set_span(self, 0, cnt0, 0);
        set_span(self, cnt0, nbits, 1);
    }
}

static PyObject *
bitarray_sort(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"reverse", NULL};
    int reverse = 0;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    sort_lock_held(self, reverse);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(sort_doc,
"sort(reverse=False)\n\
\n\
Sort all bits in bitarray (in-place).");


static PyObject *
bitarray_tolist(bitarrayobject *self)
{
    PyObject *zero = Py_GetConstant(Py_CONSTANT_ZERO);
    PyObject *one = Py_GetConstant(Py_CONSTANT_ONE);
    PyObject *list;
    Py_ssize_t nbits, i;
    int err = 1;  /* bitarray changed size */

    if (zero == NULL || one == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    list = PyList_New(nbits);
    if (list == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = 0; i < nbits; i++)
            PyList_SET_ITEM(list, i,
                            Py_NewRef(getbit(self, i) ? one : zero));
        err = 0;
    }
    Py_END_CRITICAL_SECTION();
    Py_DECREF(zero);
    Py_DECREF(one);

    if (err) {
        Py_DECREF(list);
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during .tolist()");
        return NULL;
    }
    return list;
 error:
    Py_XDECREF(zero);
    Py_XDECREF(one);
    return NULL;
}

PyDoc_STRVAR(tolist_doc,
"tolist() -> list\n\
\n\
Return bitarray as list of integers.\n\
`a.tolist()` equals `list(a)`.");


static int
frombytes_lock_held(bitarrayobject *self, const Py_buffer *view)
{
    Py_ssize_t n = Py_SIZE(self);  /* nbytes before extending */
    Py_ssize_t p = PADBITS(self);  /* number of pad bits */

    if (resize(self, 8 * (n + view->len)) < 0)
        return -1;

    if (view->len)
        memcpy(self->ob_item + n, (char *) view->buf, (size_t) view->len);

    /* remove pad bits starting at previous bit length (8 * n - p) */
    return delete_n(self, 8 * n - p, p);
}

static PyObject *
bitarray_frombytes(bitarrayobject *self, PyObject *buffer)
{
    Py_buffer view;
    int ret;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, view.obj);
    ret = frombytes_lock_held(self, &view);
    Py_END_CRITICAL_SECTION2();
    PyBuffer_Release(&view);

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(frombytes_doc,
"frombytes(bytes, /)\n\
\n\
Extend bitarray with raw bytes from a bytes-like object.\n\
Each added byte will add eight bits to the bitarray.");


static PyObject *
bitarray_tobytes(bitarrayobject *self)
{
    PyObject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    set_padbits(self);
    res = PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
    Py_END_CRITICAL_SECTION();
    return res;
}

PyDoc_STRVAR(tobytes_doc,
"tobytes() -> bytes\n\
\n\
Return the bitarray buffer (pad bits are set to zero).\n\
`a.tobytes()` is equivalent to `bytes(a)`");


/* Extend self with bytes from f.read(n).  Return number of bytes actually
   read and extended, or -1 on failure (after setting exception). */
static Py_ssize_t
extend_fread(bitarrayobject *self, PyObject *f, Py_ssize_t n)
{
    PyObject *bytes, *ret;
    Py_ssize_t res;             /* result (size or -1) */

    bytes = PyObject_CallMethod(f, "read", "n", n);
    if (bytes == NULL)
        return -1;
    if (!PyBytes_Check(bytes)) {
        PyErr_Format(PyExc_TypeError, ".read() did not return 'bytes', "
                     "got '%s'", Py_TYPE(bytes)->tp_name);
        Py_DECREF(bytes);
        return -1;
    }
    res = PyBytes_GET_SIZE(bytes);
    assert(0 <= res && res <= n);

    ret = bitarray_frombytes(self, bytes);
    Py_DECREF(bytes);
    if (ret == NULL)
        return -1;

    Py_DECREF(ret);
    return res;
}

static PyObject *
bitarray_fromfile(bitarrayobject *self, PyObject *args)
{
    PyObject *f;
    Py_ssize_t nread = 0, n = -1;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &n))
        return NULL;

    if (n < 0)  /* read till EOF */
        n = PY_SSIZE_T_MAX;

    while (nread < n) {
        Py_ssize_t nblock = Py_MIN(n - nread, BLOCKSIZE), size;

        size = extend_fread(self, f, nblock);
        if (size < 0)
            return NULL;

        nread += size;
        assert(size <= nblock && nread <= n);

        if (size < nblock) {
            if (n == PY_SSIZE_T_MAX)  /* read till EOF */
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
other binary stream that supports a `.read()` method, e.g. `io.BytesIO`).\n\
Each read byte will add eight bits to the bitarray.  When `n` is omitted\n\
or negative, reads and extends all data until EOF.\n\
When `n` is non-negative but exceeds the available data, `EOFError` is\n\
raised.  However, the available data is still read and extended.");


static PyObject *
bitarray_tofile(bitarrayobject *self, PyObject *f)
{
    Py_ssize_t nbits, nbytes, offset;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    nbytes = Py_SIZE(self);
    set_padbits(self);
    Py_END_CRITICAL_SECTION();

    for (offset = 0; offset < nbytes; offset += BLOCKSIZE) {
        Py_ssize_t size = Py_MIN(nbytes - offset, BLOCKSIZE);
        PyObject *block, *ret;
        char *dst;
        int err = 1;

        /* allocate before locking self - block object will stay private */
        block = PyBytes_FromStringAndSize(NULL, size);
        if (block == NULL)
            return NULL;
        dst = PyBytes_AS_STRING(block);

        Py_BEGIN_CRITICAL_SECTION(self);
        if (self->nbits == nbits) {
            memcpy(dst, self->ob_item + offset, (size_t) size);
            err = 0;
        }
        Py_END_CRITICAL_SECTION();

        if (err) {
            Py_DECREF(block);
            PyErr_SetString(PyExc_RuntimeError,
                            "bitarray changed size during .tofile()");
            return NULL;
        }

        ret = PyObject_CallMethod(f, "write", "O", block);
        Py_DECREF(block);
        if (ret == NULL)
            return NULL;
        Py_DECREF(ret);
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(tofile_doc,
"tofile(f, /)\n\
\n\
Write bitarray buffer to file object `f`.");


static PyObject *
bitarray_to01(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"group", "sep", NULL};
    size_t strsize, j, nsep;
    Py_ssize_t group = 0, nbits, i;
    PyObject *result;
    char *sep = " ", *str;
    int err = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ns:to01", kwlist,
                                     &group, &sep))
        return NULL;

    if (group < 0)
        return PyErr_Format(PyExc_ValueError, "non-negative integer "
                            "expected, got: %zd", group);

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    strsize = nbits;
    nsep = (group && strsize) ? strlen(sep) : 0;  /* 0 means no grouping */
    if (nsep)
        strsize += nsep * ((strsize - 1) / group);

    str = PyMem_New(char, strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = j = 0; i < nbits; i++) {
            if (nsep && i && i % group == 0) {
                memcpy(str + j, sep, nsep);
                j += nsep;
            }
            str[j++] = getbit(self, i) + '0';
        }
        assert(j == strsize);
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        PyMem_Free((void *) str);
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during .to01()");
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(str, strsize);
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(to01_doc,
"to01(group=0, sep=' ') -> str\n\
\n\
Return bitarray as (Unicode) string of `0`s and `1`s.\n\
The bits are grouped into `group` bits (default is no grouping).\n\
When grouped, the string `sep` is inserted between groups\n\
of `group` characters, default is a space.");


static PyObject *
bitarray_unpack(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"zero", "one", NULL};
    PyObject *res;
    char zero = 0x00, one = 0x01, *str;
    Py_ssize_t nbits, i;
    int err = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    res = PyBytes_FromStringAndSize(NULL, nbits);
    if (res == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        str = PyBytes_AsString(res);
        for (i = 0; i < nbits; i++)
            str[i] = getbit(self, i) ? one : zero;
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during .unpack()");
        return NULL;
    }
    return res;
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\x01') -> bytes\n\
\n\
Return bytes that contain one byte for each bit in the bitarray,\n\
using the specified mapping.");


static PyObject *
bitarray_pack(bitarrayobject *self, PyObject *buffer)
{
    Py_ssize_t nbits, i;
    Py_buffer view;
    int ret;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, view.obj);
    nbits = self->nbits;
    ret = resize(self, nbits + view.len);
    if (ret == 0) {
        for (i = 0; i < view.len; i++)
            setbit(self, nbits + i, ((char *) view.buf)[i]);
    }
    Py_END_CRITICAL_SECTION2();

    PyBuffer_Release(&view);
    if (ret < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(pack_doc,
"pack(bytes, /)\n\
\n\
Extend bitarray from a bytes-like object, where each byte corresponds\n\
to a single bit.  The byte `b'\\x00'` maps to bit 0 and all other bytes\n\
map to bit 1.");


/* Pop and return bit 0 or 1 while self is locked; return -1 on error. */
static int
bitarray_pop_lock_held(bitarrayobject *self, Py_ssize_t i)
{
    Py_ssize_t n = self->nbits;
    int vi;

    if (n == 0) {
        /* special case -- most common failure cause */
        PyErr_SetString(PyExc_IndexError, "pop from empty bitarray");
        return -1;
    }

    if (i < 0)
        i += n;

    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "pop index out of range");
        return -1;
    }

    vi = getbit(self, i);
    if (delete_n(self, i, 1) < 0)
        return -1;

    return vi;
}

static PyObject *
bitarray_pop(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t i = -1;
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|n:pop", &i))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    vi = bitarray_pop_lock_held(self, i);
    Py_END_CRITICAL_SECTION();

    if (vi < 0)
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
    int ret = -1, vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    i = find_bit(self, vi, 0, self->nbits, 0);
    if (i < 0) {
        PyErr_Format(PyExc_ValueError, "%d not in bitarray", vi);
    }
    else {
        ret = delete_n(self, i, 1);
    }
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(remove_doc,
"remove(value, /)\n\
\n\
Remove the first occurrence of `value`.\n\
Raises `ValueError` if value is not present.");


static void
rotate_lock_held(bitarrayobject *self, bitarrayobject *tmp, Py_ssize_t k)
{
    Py_ssize_t n = self->nbits;

    assert(tmp->nbits <= n / 2);  /* at most half size */

    if (tmp->nbits == k) {           /* tail is smaller */
        copy_n(tmp, 0, self, n - k, k);   /* save tail */
        copy_n(self, k, self, 0, n - k);  /* shift array right by k */
        copy_n(self, 0, tmp, 0, k);       /* copy stored tail at front */
    }
    else if (tmp->nbits == n - k) {  /* head is smaller */
        copy_n(tmp, 0, self, 0, n - k);   /* save head */
        copy_n(self, 0, self, n - k, k);  /* shift array left by n-k */
        copy_n(self, k, tmp, 0, n - k);   /* copy stored head at end */
    }
    else {
        Py_UNREACHABLE();
    }
}

static PyObject *
bitarray_rotate(bitarrayobject *self, PyObject *args)
{
    bitarrayobject *tmp;
    Py_ssize_t n, k = 1;
    int err = 1;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|n:rotate", &k))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    n = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (n < 2)
        Py_RETURN_NONE;

    k %= n;  /* C remainder may be negative, as it preserves the sign of k */
    if (k < 0)
        k += n;  /* make it equivalent to Python's k %= n */
    if (k == 0)
        Py_RETURN_NONE;

    assert(0 < k && k < n);

    /* temporary bitarray to store head or tail (whichever is smaller) */
    tmp = newbitarrayobject(&Bitarray_Type, Py_MIN(k, n - k), self->endian);
    if (tmp == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == n) {
        rotate_lock_held(self, tmp, k);
        err = 0;
    }
    Py_END_CRITICAL_SECTION();
    Py_DECREF(tmp);

    if (err) {
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during .rotate()");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(rotate_doc,
"rotate(k=1, /)\n\
\n\
Rotate bitarray in-place by `k` positions.\n\
Positive `k` rotates right, negative `k` rotates left.");


static PyObject *
bitarray_sizeof(bitarrayobject *self)
{
    Py_ssize_t res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = sizeof(bitarrayobject) + self->allocated;
    if (self->buffer)
        res += sizeof(Py_buffer);
    Py_END_CRITICAL_SECTION();
    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof_doc, "Return size of bitarray object in bytes.");


/* private method - called only when frozenbitarray is initialized to
   disallow memoryviews to change the buffer */
static PyObject *
bitarray_freeze(bitarrayobject *self)
{
    if (self->buffer) {
        assert(self->buffer->readonly == self->readonly);
        if (self->readonly == 0) {
            PyErr_SetString(PyExc_TypeError, "cannot import writable "
                            "buffer into frozenbitarray");
            return NULL;
        }
    }
    set_padbits(self);
    self->readonly = 1;
    Py_RETURN_NONE;
}

/* -------- bitarray methods exposed in debug mode for testing ---------- */

#ifndef NDEBUG

static PyObject *
bitarray_shift_r8(bitarrayobject *self, PyObject *args)
{
    Py_ssize_t a, b;
    int n;

    if (!PyArg_ParseTuple(args, "nni", &a, &b, &n))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    shift_r8(self, a, b, n);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

static PyObject *
bitarray_copy_n(bitarrayobject *self, PyObject *args)
{
    PyObject *other;
    Py_ssize_t a, b, n;

    if (!PyArg_ParseTuple(args, "nO!nn", &a, &Bitarray_Type, &other, &b, &n))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    copy_n(self, a, (bitarrayobject *) other, b, n);
    Py_END_CRITICAL_SECTION2();
    Py_RETURN_NONE;
}

static PyObject *
bitarray_overlap(bitarrayobject *self, PyObject *other)
{
    int res;

    assert(bitarray_Check(other));

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    res = buffers_overlap(self, (bitarrayobject *) other);
    Py_END_CRITICAL_SECTION2();

    return PyBool_FromLong(res);
}

#endif  /* NDEBUG */

/* ---------------------- bitarray getset members ---------------------- */

static PyObject *
bitarray_get_endian(bitarrayobject *self, void *Py_UNUSED(ignored))
{
    return PyUnicode_FromString(ENDIAN_STR(self->endian));
}

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

static PyGetSetDef bitarray_getsets[] = {
    {"endian", (getter) bitarray_get_endian, NULL,
     PyDoc_STR("bit-endianness as Unicode string")},
    {"nbytes", (getter) bitarray_get_nbytes, NULL,
     PyDoc_STR("buffer size in bytes")},
    {"padbits", (getter) bitarray_get_padbits, NULL,
     PyDoc_STR("number of pad bits")},
    {"readonly", (getter) bitarray_get_readonly, NULL,
     PyDoc_STR("bool indicating whether buffer is read-only")},
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
    bitarrayobject *res;
    int ret;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = bitarray_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    if (bitarray_Check(other)) {
        Py_BEGIN_CRITICAL_SECTION(other);
        ret = extend_bitarray(res, (bitarrayobject *) other);
        Py_END_CRITICAL_SECTION();
    }
    else {
        ret = extend_dispatch(res, other);
    }

    if (ret < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return freeze_if_frozen(res);
}

static PyObject *
bitarray_repeat(bitarrayobject *self, Py_ssize_t n)
{
    bitarrayobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = bitarray_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    if (repeat(res, n) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return freeze_if_frozen(res);
}

static int
bitarray_item_lock_held(bitarrayobject *self, Py_ssize_t i)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError, "bitarray index out of range");
        return -1;
    }
    return getbit(self, i);
}

static PyObject *
bitarray_item(bitarrayobject *self, Py_ssize_t i)
{
    int vi;

    Py_BEGIN_CRITICAL_SECTION(self);
    vi = bitarray_item_lock_held(self, i);
    Py_END_CRITICAL_SECTION();

    return (vi < 0) ? NULL : PyLong_FromLong(vi);
}

/* vi is 0 or 1 for assignment, and 2 for deletion. */
static int
bitarray_ass_item_lock_held(bitarrayobject *self, Py_ssize_t i, int vi)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError,
                        "bitarray assignment index out of range");
        return -1;
    }

    if (vi == 2) {
        return delete_n(self, i, 1);
    }

    setbit(self, i, vi);
    return 0;
}

static int
bitarray_ass_item(bitarrayobject *self, Py_ssize_t i, PyObject *value)
{
    int vi, res;

    RAISE_IF_READONLY(self, -1);
    if (value != NULL && !conv_pybit(value, &vi))
        return -1;

    if (value == NULL)  /* delete item */
        vi = 2;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = bitarray_ass_item_lock_held(self, i, vi);
    Py_END_CRITICAL_SECTION();

    return res;
}

/* return 1 if value (which can be an int or bitarray) is in self,
   0 otherwise, and -1 on error */
static int
bitarray_contains(bitarrayobject *self, PyObject *value)
{
    Py_ssize_t pos;

    if (PyIndex_Check(value)) {
        int vi;

        if (!conv_pybit(value, &vi))
            return -1;

        Py_BEGIN_CRITICAL_SECTION(self);
        pos = find_bit(self, vi, 0, self->nbits, 0);
        Py_END_CRITICAL_SECTION();
    }
    else if (bitarray_Check(value)) {
        Py_BEGIN_CRITICAL_SECTION2(self, value);
        pos = find_sub(self, (bitarrayobject *) value, 0, self->nbits, 0);
        Py_END_CRITICAL_SECTION2();
    }
    else {
        PyErr_Format(PyExc_TypeError, "sub_bitarray must be bitarray or "
                     "int, not '%s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    return pos >= 0;
}

static PyObject *
bitarray_inplace_concat(bitarrayobject *self, PyObject *other)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_thread_safe(self, other) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
bitarray_inplace_repeat(bitarrayobject *self, Py_ssize_t n)
{
    int ret;

    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = repeat(self, n);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
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

/* return new bitarray with item in self, specified by slice indices */
static PyObject *
getslice_indices_lock_held(bitarrayobject *self, Py_ssize_t start,
                           Py_ssize_t step, Py_ssize_t slicelength)
{
    bitarrayobject *res;

    res = newbitarrayobject(Py_TYPE(self), slicelength, self->endian);
    if (res == NULL)
        return NULL;

    if (step == 1) {
        copy_n(res, 0, self, start, slicelength);
    }
    else {
        Py_ssize_t i, j;

        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(res, i, getbit(self, j));
    }
    return freeze_if_frozen(res);
}

/* return new bitarray with item in self, specified by slice */
static PyObject *
getslice(bitarrayobject *self, PyObject *slice)
{
    PyObject *res;
    Py_ssize_t start, stop, step, slicelength;

    assert(PySlice_Check(slice));
    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    res = getslice_indices_lock_held(self, start, step, slicelength);
    Py_END_CRITICAL_SECTION();

    return res;
}

static int
ensure_mask_size(bitarrayobject *self, bitarrayobject *mask)
{
    if (self->nbits != mask->nbits) {
        PyErr_Format(PyExc_IndexError, "bitarray length is %zd, but "
                     "mask has length %zd", self->nbits, mask->nbits);
        return -1;
    }
    return 0;
}

static PyObject *
getmask_lock_held(bitarrayobject *self, bitarrayobject *mask)
{
    bitarrayobject *res;
    Py_ssize_t n, i, j;

    if (ensure_mask_size(self, mask) < 0)
        return NULL;

    n = count_span(mask, 0, mask->nbits);
    res = newbitarrayobject(Py_TYPE(self), n, self->endian);
    if (res == NULL)
        return NULL;

    for (i = j = 0; i < mask->nbits; i++) {
        if (getbit(mask, i))
            setbit(res, j++, getbit(self, i));
    }
    assert(j == n);
    return (PyObject *) res;
}

/* return a new bitarray with items from 'self' masked by bitarray 'mask' */
static PyObject *
getmask(bitarrayobject *self, bitarrayobject *mask)
{
    PyObject *res = NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    res = getmask_lock_held(self, mask);
    Py_END_CRITICAL_SECTION2();

    if (res == NULL)
        return NULL;
    return freeze_if_frozen((bitarrayobject *) res);
}

/* Return j-th item from sequence.  The item is considered an index into
   an array with given length, and is normalized pythonically.
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

/* Convert 'seq' to an array of indices normalized to 'length'.
   On success, store the allocated array in '*indices', its number of
   elements in '*size', and return 0.  The array may be NULL when
   '*size' is zero and must be freed with PyMem_Free().
   On error, leave the output arguments unchanged and return -1 with
   an exception set. */
static int
sequence_as_array(PyObject *seq, Py_ssize_t length,
                  Py_ssize_t **indices, Py_ssize_t *size)
{
    Py_ssize_t *p = NULL;
    Py_ssize_t n, j;

    n = PySequence_Size(seq);  /* may execute arbitrary Python code */
    if (n < 0)
        return -1;

    if (n != 0) {
        p = PyMem_New(Py_ssize_t, n);
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
    }

    for (j = 0; j < n; j++) {
        Py_ssize_t i = index_from_seq(seq, j, length);
        if (i < 0) {
            PyMem_Free(p);
            return -1;
        }
        p[j] = i;
    }

    *indices = p;
    *size = n;
    return 0;
}

/* return a new bitarray with items from 'self' listed by
   sequence (of indices) 'seq' */
static PyObject *
getsequence(bitarrayobject *self, PyObject *seq)
{
    bitarrayobject *res;
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n, j;
    int err = 1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return NULL;

    if ((res = newbitarrayobject(Py_TYPE(self), n, self->endian)) == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (j = 0; j < n; j++)
            setbit(res, j, getbit(self, indices[j]));
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    PyMem_Free(indices);

    if (err) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during sequence indexing");
        return NULL;
    }
    return freeze_if_frozen(res);

 error:
    PyMem_Free(indices);
    return NULL;
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
        int vi;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        vi = bitarray_item_lock_held(self, i);
        Py_END_CRITICAL_SECTION();

        return (vi < 0) ? NULL : PyLong_FromLong(vi);
    }

    if (PySlice_Check(item))
        return getslice(self, item);

    if (bitarray_Check(item))
        return getmask(self, (bitarrayobject *) item);

    if (subscr_seq_check(item) < 0)
        return NULL;

    return getsequence(self, item);
}

/* The following functions are called from assign_slice(). */

static int
setslice_lock_held(bitarrayobject *self, bitarrayobject *other,
                   Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    Py_ssize_t slicelength, increase;

    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);

    /* number of bits by which self has to be increased (decreased) */
    increase = other->nbits - slicelength;

    if (step == 1) {
        if (increase > 0) {        /* increase self */
            if (insert_n(self, start + slicelength, increase) < 0)
                return -1;
        }
        if (increase < 0) {        /* decrease self */
            if (delete_n(self, start + other->nbits, -increase) < 0)
                return -1;
        }
        /* copy new values into self */
        copy_n(self, start, other, 0, other->nbits);
    }
    else {
        Py_ssize_t i, j;

        if (increase != 0) {
            PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                         "size %zd to extended slice of size %zd",
                         other->nbits, slicelength);
            return -1;
        }
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, getbit(other, i));
    }
    return 0;
}

/* set items in self, specified by slice, to other bitarray */
static int
setslice_bitarray(bitarrayobject *self, PyObject *slice,
                  bitarrayobject *other)
{
    bitarrayobject *copy = NULL;
    bitarrayobject *src = other;
    Py_ssize_t start, stop, step;
    int res;

    assert(PySlice_Check(slice));
    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    /* Make a copy of other, in case the buffers overlap.  This is obviously
       the case when self and other are the same object, but can also happen
       when the two bitarrays share memory. */
    if (buffers_overlap(self, other)) {
        copy = bitarray_cp(other);
        src = copy;
    }
    if (src == NULL)
        res = -1;  /* bitarray_cp() failed */
    else
        res = setslice_lock_held(self, src, start, stop, step);
    Py_END_CRITICAL_SECTION2();

    Py_XDECREF(copy);
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

    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    adjust_step_positive(slicelength, &start, &stop, &step);
    set_range(self, start, stop, step, vi);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static int
delslice_lock_held(bitarrayobject *self,
                   Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    Py_ssize_t slicelength;

    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    adjust_step_positive(slicelength, &start, &stop, &step);

    if (step > 1) {
        /* set items not to be removed (up to stop) */
        Py_ssize_t i = start + 1, j = start;

        if (step >= 4) {
            for (; i < stop; i += step) {
                Py_ssize_t length = Py_MIN(step - 1, stop - i);
                copy_n(self, j, self, i, length);
                j += length;
            }
        }
        else {
            for (; i < stop; i++) {
                if ((i - start) % step != 0)
                    setbit(self, j++, getbit(self, i));
            }
        }
        assert(slicelength == 0 || j == stop - slicelength);
    }
    return delete_n(self, stop - slicelength, slicelength);
}

/* delete items in self, specified by slice */
static int
delslice(bitarrayobject *self, PyObject *slice)
{
    Py_ssize_t start, stop, step;
    int ret;

    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = delslice_lock_held(self, start, stop, step);
    Py_END_CRITICAL_SECTION();
    return ret;
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

    PyErr_Format(PyExc_TypeError, "bitarray or int expected for slice "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

/* The following functions are called from assign_mask(). */

/* assign mask of bitarray self to bitarray other */
static int
setmask_bitarray_lock_held(bitarrayobject *self, bitarrayobject *mask,
                           bitarrayobject *other)
{
    Py_ssize_t n, i, j;

    assert(self->nbits == mask->nbits);

    n = count_span(mask, 0, mask->nbits);  /* mask size */
    if (n != other->nbits) {
        PyErr_Format(PyExc_IndexError, "attempt to assign mask of size %zd "
                     "to bitarray of size %zd", n, other->nbits);
        return -1;
    }

    for (i = j = 0; i < mask->nbits; i++) {
        if (getbit(mask, i))
            setbit(self, i, getbit(other, j++));
    }
    assert(j == n);
    return 0;
}

static int
setmask_bitarray(bitarrayobject *self, bitarrayobject *mask,
                 bitarrayobject *other)
{
    bitarrayobject *src;
    int res = -1;

#ifdef Py_GIL_DISABLED
    /* copy other so the operation below only needs to lock self and mask */
    Py_BEGIN_CRITICAL_SECTION(other);
    src = bitarray_cp(other);
    Py_END_CRITICAL_SECTION();
#else
    src = (bitarrayobject *) Py_NewRef(other);
#endif

    if (src == NULL)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    if (ensure_mask_size(self, mask) == 0)
        res = setmask_bitarray_lock_held(self, mask, src);
    Py_END_CRITICAL_SECTION2();

    Py_DECREF(src);
    return res;
}

/* assign mask of bitarray self to boolean value */
static int
setmask_bool(bitarrayobject *self, bitarrayobject *mask, PyObject *value)
{
    static char *expr[] = {"a &= ~mask",  /* a[mask] = 0 */
                           "a |= mask"};  /* a[mask] = 1 */
    int vi;

    if (!conv_pybit(value, &vi))
        return -1;

    PyErr_Format(PyExc_NotImplementedError, "mask assignment to bool not "
                 "implemented;\n`a[mask] = %d` equivalent to `%s`",
                 vi, expr[vi]);
    return -1;
}

/* delete items in self, specified by mask */
static int
delmask_lock_held(bitarrayobject *self, bitarrayobject *mask)
{
    Py_ssize_t n = 0, i;

    assert(self->nbits == mask->nbits);
    for (i = 0; i < mask->nbits; i++) {
        if (getbit(mask, i) == 0)  /* set items we want to keep */
            setbit(self, n++, getbit(self, i));
    }
    assert(self == mask ||
           n == mask->nbits - count_span(mask, 0, mask->nbits));

    return resize(self, n);
}

static int
delmask(bitarrayobject *self, bitarrayobject *mask)
{
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    if (ensure_mask_size(self, mask) == 0)
        res = delmask_lock_held(self, mask);
    Py_END_CRITICAL_SECTION2();

    return res;
}

/* assign mask of bitarray self to value */
static int
assign_mask(bitarrayobject *self, bitarrayobject *mask, PyObject *value)
{
    if (value == NULL)
        return delmask(self, mask);

    if (bitarray_Check(value))
        return setmask_bitarray(self, mask, (bitarrayobject *) value);

    if (PyIndex_Check(value))
        return setmask_bool(self, mask, value);

    PyErr_Format(PyExc_TypeError, "bitarray or int expected for mask "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

/* The following functions are called from assign_sequence(). */

/* assign sequence (of indices) of bitarray self to bitarray */
static int
setseq_bitarray(bitarrayobject *self, PyObject *seq, bitarrayobject *other)
{
    bitarrayobject *copy = NULL;
    bitarrayobject *src = other;
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n;
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    if (n != other->nbits) {
        PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                     "size %zd to bitarray of size %zd", n, other->nbits);
    }
    else if (self->nbits != nbits) {
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during sequence indexing");
    }
    else {
        /* Make a copy of other, see comment in setslice_bitarray(). */
        if (buffers_overlap(self, other)) {
            copy = bitarray_cp(other);
            src = copy;
        }
        if (src) {
            Py_ssize_t j;
            for (j = 0; j < n; j++)
                setbit(self, indices[j], getbit(src, j));
            res = 0;
        }
    }
    Py_END_CRITICAL_SECTION2();

    PyMem_Free(indices);
    Py_XDECREF(copy);
    return res;
}

/* assign sequence (of indices) of bitarray self to Boolean value */
static int
setseq_bool(bitarrayobject *self, PyObject *seq, PyObject *value)
{
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n;
    int res = -1, vi;

    if (!conv_pybit(value, &vi))
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        Py_ssize_t j;
        for (j = 0; j < n; j++)
            setbit(self, indices[j], vi);
        res = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (res < 0)
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during sequence indexing");

    PyMem_Free(indices);
    return res;
}

/* Materialize 'seq' into indices normalized to 'length' as
   a bitarray of 'length'. */
static bitarrayobject *
sequence_as_bitarray(PyObject *seq, Py_ssize_t length)
{
    bitarrayobject *res;
    Py_ssize_t n, j;

    n = PySequence_Size(seq);  /* may execute arbitrary Python code */
    if (n < 0)
        return NULL;

    res = newbitarrayobject(&Bitarray_Type, length, ENDIAN_DEFAULT);
    if (res == NULL)
        return NULL;

    if (res->ob_item)
        memset(res->ob_item, 0x00, (size_t) Py_SIZE(res));

    /* set indices from sequence */
    for (j = 0; j < n; j++) {
        Py_ssize_t i = index_from_seq(seq, j, length);
        if (i < 0) {
            Py_DECREF(res);
            return NULL;
        }
        setbit(res, i, 1);
    }
    return res;
}

/* delete items in self, specified by sequence of indices */
static int
delsequence(bitarrayobject *self, PyObject *seq)
{
    Py_ssize_t nbits;
    bitarrayobject *mask;  /* temporary bitarray masking items to remove */
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    mask = sequence_as_bitarray(seq, nbits);
    if (mask == NULL)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        res = delmask_lock_held(self, mask);  /* do actual work here */
    }
    else {
        PyErr_SetString(PyExc_RuntimeError,
                        "bitarray changed size during sequence indexing");
    }
    Py_END_CRITICAL_SECTION();

    Py_DECREF(mask);
    return res;
}

/* assign sequence (of indices) of bitarray self to value */
static int
assign_sequence(bitarrayobject *self, PyObject *seq, PyObject *value)
{
    assert(PySequence_Check(seq));

    if (value == NULL)
        return delsequence(self, seq);

    if (bitarray_Check(value))
        return setseq_bitarray(self, seq, (bitarrayobject *) value);

    if (PyIndex_Check(value))
        return setseq_bool(self, seq, value);

    PyErr_Format(PyExc_TypeError, "bitarray or int expected for sequence "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

static int
bitarray_ass_subscr(bitarrayobject *self, PyObject *item, PyObject *value)
{
    RAISE_IF_READONLY(self, -1);

    if (PyIndex_Check(item)) {
        Py_ssize_t i;
        int vi, res;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;

        if (value != NULL && !conv_pybit(value, &vi))
            return -1;

        if (value == NULL)  /* delete item */
            vi = 2;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        res = bitarray_ass_item_lock_held(self, i, vi);
        Py_END_CRITICAL_SECTION();

        return res;
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
    bitarrayobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = bitarray_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    invert_span(res, 0, res->nbits);
    return freeze_if_frozen(res);
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
    assert(self->readonly == 0);
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
   bit-endianness.  Otherwise, set exception and return -1. */
static int
bitwise_check(PyObject *a, PyObject *b, const char *ostr)
{
    if (!bitarray_Check(a) || !bitarray_Check(b)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
        return -1;
    }
    return ensure_eq_size_endian((bitarrayobject *) a, (bitarrayobject *) b);
}

#define BITWISE_FUNC(name, inplace, ostr)                   \
static PyObject *                                           \
bitarray_ ## name (PyObject *self, PyObject *other)         \
{                                                           \
    bitarrayobject *res = NULL;                             \
                                                            \
    if (inplace)                                            \
        RAISE_IF_READONLY(self, NULL);                      \
                                                            \
    Py_BEGIN_CRITICAL_SECTION2(self, other);                \
    if (bitwise_check(self, other, ostr) == 0) {            \
        res = inplace                                       \
            ? (bitarrayobject *) Py_NewRef(self)            \
            : bitarray_cp((bitarrayobject *) self);         \
                                                            \
        if (res)                                            \
            bitwise(res, (bitarrayobject *) other, *ostr);  \
    }                                                       \
    Py_END_CRITICAL_SECTION2();                             \
    if (res == NULL)                                        \
        return NULL;                                        \
    if (!inplace)                                           \
        return freeze_if_frozen(res);                       \
    return (PyObject *) res;                                \
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

    assert(n >= 0 && self->readonly == 0);
    if (n > nbits)
        n = nbits;

    assert(n <= nbits);
    if (right) {                /* rshift */
        copy_n(self, n, self, 0, nbits - n);
        set_span(self, 0, n, 0);
    }
    else {                      /* lshift */
        copy_n(self, 0, self, n, nbits - n);
        set_span(self, nbits - n, nbits, 0);
    }
}

/* check shift arguments and return shift count, -1 on error */
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

#define SHIFT_FUNC(name, inplace, ostr)                     \
static PyObject *                                           \
bitarray_ ## name (PyObject *self, PyObject *other)         \
{                                                           \
    bitarrayobject *res = NULL;                             \
    Py_ssize_t n;                                           \
                                                            \
    if ((n = shift_check(self, other, ostr)) < 0)           \
        return NULL;                                        \
                                                            \
    if (inplace)                                            \
        RAISE_IF_READONLY(self, NULL);                      \
                                                            \
    Py_BEGIN_CRITICAL_SECTION(self);                        \
    res = inplace                                           \
        ? (bitarrayobject *) Py_NewRef(self)                \
        : bitarray_cp((bitarrayobject *) self);             \
                                                            \
    if (res)                                                \
        shift(res, n, *ostr == '>');                        \
    Py_END_CRITICAL_SECTION();                              \
                                                            \
    if (res == NULL)                                        \
        return NULL;                                        \
    if (!inplace)                                           \
        return freeze_if_frozen(res);                       \
    return (PyObject *) res;                                \
}

SHIFT_FUNC(lshift,  0, "<<")  /* bitarray_lshift */
SHIFT_FUNC(rshift,  0, ">>")  /* bitarray_rshift */
SHIFT_FUNC(ilshift, 1, "<<=") /* bitarray_ilshift */
SHIFT_FUNC(irshift, 1, ">>=") /* bitarray_irshift */


static PyNumberMethods bitarray_as_number = {
    0,                                   /* nb_add */
    0,                                   /* nb_subtract */
    0,                                   /* nb_multiply */
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
    0,                                   /* nb_int */
    0,                                   /* nb_reserved (was nb_long) */
    0,                                   /* nb_float */
    0,                                   /* nb_inplace_add */
    0,                                   /* nb_inplace_subtract */
    0,                                   /* nb_inplace_multiply */
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
    0,                                   /* nb_index */
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
         PyErr_SetString(PyExc_TypeError, "bitarray expected for dict value");
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

    if ((iter = PyObject_GetIter(iterable)) == NULL)
        return PyErr_Format(PyExc_TypeError, "'%s' object is not iterable",
                            Py_TYPE(iterable)->tp_name);

    /* extend self with the bitarrays from codedict */
    while ((symbol = PyIter_Next(iter))) {
        int ret;

        if (PyDict_GetItemRef(codedict, symbol, &value) < 0)
            goto error;

        if (value == NULL) {
            PyErr_Format(PyExc_ValueError,
                         "symbol not defined in prefix code: %A", symbol);
            goto error;
        }
        Py_BEGIN_CRITICAL_SECTION2(self, value);
        ret = check_value(value);
        if (ret == 0)
            ret = extend_bitarray(self, (bitarrayobject *) value);
        Py_END_CRITICAL_SECTION2();
        if (ret < 0)
            goto error;

        Py_DECREF(symbol);
        Py_DECREF(value);
    }
    Py_DECREF(iter);
    if (PyErr_Occurred())       /* from PyIter_Next() */
        return NULL;
    Py_RETURN_NONE;

 error:
    Py_DECREF(iter);
    Py_DECREF(symbol);
    Py_XDECREF(value);
    return NULL;
}

PyDoc_STRVAR(encode_doc,
"encode(code, iterable, /)\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
iterate over the iterable object with symbols, and extend bitarray\n\
with corresponding bitarray for each symbol.");

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

    nd = PyMem_New(binode, 1);
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

/* insert symbol (mapping to bitarray a) into tree */
static int
binode_insert_symbol(binode *tree, bitarrayobject *a, PyObject *symbol)
{
    binode *nd = tree, *prev;
    Py_ssize_t i;

    for (i = 0; i < a->nbits; i++) {
        int k = getbit(a, i);

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
    PyErr_Format(PyExc_ValueError, "prefix code ambiguous: %A", symbol);
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
    int ret = 0;

    tree = binode_new();
    if (tree == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(codedict);
    while (PyDict_Next(codedict, &pos, &symbol, &value)) {
        /* Keep the current borrowed references alive if a helper suspends
         * the critical section. */
        Py_INCREF(symbol);
        Py_INCREF(value);

        Py_BEGIN_CRITICAL_SECTION(value);
        ret = check_value(value);
        if (ret == 0) {
            ret = binode_insert_symbol(
                tree, (bitarrayobject *) value, symbol);
        }
        Py_END_CRITICAL_SECTION();
        Py_DECREF(value);
        Py_DECREF(symbol);

        if (ret < 0)
            break;
    }
    Py_END_CRITICAL_SECTION();

    if (ret < 0) {
        binode_delete(tree);
        return NULL;
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
        return PyDict_SetItem(dict, nd->symbol, (PyObject *) prefix);
    }

    for (k = 0; k < 2; k++) {
        bitarrayobject *t;      /* prefix of the two child nodes */
        int ret;

        if ((t = bitarray_cp(prefix)) == NULL)
            return -1;
        if (resize(t, t->nbits + 1) < 0) {
            Py_DECREF(t);
            return -1;
        }
        setbit(t, t->nbits - 1, k);
        ret = binode_to_dict(nd->child[k], dict, t);
        Py_DECREF(t);
        if (ret < 0)
            return -1;
    }
    return 0;
}

/* return whether node is complete (has both children or is a symbol node) */
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

/* return number of nodes */
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
    PyObject *codedict, *obj;

    if (!PyArg_ParseTuple(args, "O:decodetree", &codedict))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = binode_make_tree(codedict);
    if (tree == NULL)
        return NULL;

    obj = type->tp_alloc(type, 0);
    if (obj == NULL) {
        binode_delete(tree);
        return NULL;
    }
    ((decodetreeobject *) obj)->tree = tree;

    return obj;
}

static PyObject *
decodetree_todict(decodetreeobject *self)
{
    PyObject *dict;
    bitarrayobject *prefix;

    if ((dict = PyDict_New()) == NULL)
        return NULL;

    prefix = newbitarrayobject(&Bitarray_Type, 0, ENDIAN_DEFAULT);
    if (prefix == NULL)
        goto error;

    if (binode_to_dict(self->tree, dict, prefix) < 0)
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
reconstruction of the code dict which the object was created with.");


static PyObject *
decodetree_complete(decodetreeobject *self)
{
    return PyBool_FromLong(binode_complete(self->tree));
}

PyDoc_STRVAR(complete_doc,
"complete() -> bool\n\
\n\
Return whether tree is complete.  That is, whether or not all\n\
nodes have both children (unless they are symbol nodes).");


static PyObject *
decodetree_nodes(decodetreeobject *self)
{
    return PyLong_FromSsize_t(binode_nodes(self->tree));
}

PyDoc_STRVAR(nodes_doc,
"nodes() -> int\n\
\n\
Return number of nodes in tree (internal and symbol nodes).");


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
    {NULL,         NULL}  /* sentinel */
};

PyDoc_STRVAR(decodetree_doc,
"decodetree(code, /) -> decodetree\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays),\n\
create a binary tree object to be passed to `.decode()`.");

static PyTypeObject DecodeTree_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
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
    if (DecodeTree_Check(obj))
        return ((decodetreeobject *) obj)->tree;

    if (check_codedict(obj) < 0)
        return NULL;

    return binode_make_tree(obj);
}

/*********************** (bitarray) Decode Iterator ***********************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *self;       /* bitarray we're decoding */
    binode *tree;               /* prefix tree containing symbols */
    Py_ssize_t index;           /* current index in bitarray */
    PyObject *decodetree;       /* decodetree or NULL */
} decodeiterobject;

static PyTypeObject DecodeIter_Type;

/* create a new initialized bitarray decode iterator object */
static PyObject *
bitarray_decode(bitarrayobject *self, PyObject *obj)
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
    it->self = self;
    it->tree = tree;
    it->index = 0;
    it->decodetree = DecodeTree_Check(obj) ? obj : NULL;
    Py_XINCREF(it->decodetree);
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(decode_doc,
"decode(code, /) -> decodeiterator\n\
\n\
Given a prefix code (a dict mapping symbols to bitarrays, or `decodetree`\n\
object), decode content of bitarray and return an iterator over\n\
corresponding symbols.");


static PyObject *
decodeiter_next(decodeiterobject *it)
{
    PyObject *symbol;

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    /* may be NULL when stop iteration OR error occurred */
    symbol = binode_traverse(it->tree, it->self, &(it->index));
    Py_END_CRITICAL_SECTION2();

    Py_XINCREF(symbol);
    return symbol;
}

static void
decodeiter_dealloc(decodeiterobject *it)
{
    PyObject_GC_UnTrack(it);
    if (it->decodetree)
        Py_DECREF(it->decodetree);
    else       /* when decodeiter was created from dict - free tree */
        binode_delete(it->tree);

    Py_DECREF(it->self);
    PyObject_GC_Del(it);
}

static int
decodeiter_traverse(decodeiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    Py_VISIT(it->decodetree);
    return 0;
}

static PyObject *
decodeiter_skipbits(decodeiterobject *it, PyObject *args)
{
    PyObject *skipped = NULL;
    Py_ssize_t n;  /* number of bits to skip */

    if (!PyArg_ParseTuple(args, "n:skipbits", &n))
        return NULL;

    if (n < 0)
        return PyErr_Format(PyExc_ValueError, "skip count cannot be "
                            "negative, got %zd", n);

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    if (n <= it->self->nbits - it->index) {
        skipped = getslice_indices_lock_held(it->self, it->index, 1, n);
        if (skipped)
            it->index += n;
    }
    else {
        PyErr_Format(PyExc_ValueError, "skip count %zd cannot be "
                     "larger than remaining bits %zd",
                     n, it->self->nbits - it->index);
    }
    Py_END_CRITICAL_SECTION2();

    return skipped;
}

PyDoc_STRVAR(decodeiter_skipbits_doc,
"skipbits(n, /) -> bitarray\n\
\n\
Skip over the next `n` bits and return them.\n\
Raises `ValueError` if count is out of range.");


static PyMethodDef decodeiter_methods[] = {
    {"skipbits",    (PyCFunction) decodeiter_skipbits, METH_VARARGS,
     decodeiter_skipbits_doc},
    {NULL}
};

static PyMemberDef decodeiter_members[] = {
    {"index", Py_T_PYSSIZET, offsetof(decodeiterobject, index), Py_READONLY,
     PyDoc_STR("current bit position to be decoded by subsequent `next`")},
    {NULL}
};

static PyTypeObject DecodeIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
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
    decodeiter_methods,                       /* tp_methods */
    decodeiter_members,                       /* tp_members */
};

/*********************** (Bitarray) Search Iterator ***********************/

/* Note: when .sub is NULL search for single bit value in member .vi */
typedef struct {
    PyObject_HEAD
    bitarrayobject *self;   /* bitarray we're searching in */
    bitarrayobject *sub;    /* bitarray being searched for */
    int vi;                 /* single bit being searched for */
    Py_ssize_t start;
    Py_ssize_t stop;
    int right;
} searchiterobject;

static PyTypeObject SearchIter_Type;

/* create a new initialized bitarray search iterator object */
static PyObject *
bitarray_search(bitarrayobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "", "", "right", NULL};
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX;
    int vi = -1, right = 0;
    PyObject *sub;
    searchiterobject *it;  /* iterator to be returned */

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|nni", kwlist,
                                     &sub, &start, &stop, &right))
        return NULL;

    if (PyIndex_Check(sub)) {
        if (!conv_pybit(sub, &vi))
            return NULL;
    }
    else if (!bitarray_Check(sub)) {
        return PyErr_Format(PyExc_TypeError, "sub_bitarray must be bitarray "
                            "or int, not '%s'", Py_TYPE(sub)->tp_name);
    }

    it = PyObject_GC_New(searchiterobject, &SearchIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    Py_BEGIN_CRITICAL_SECTION(self);
    PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
    it->start = start;
    it->stop = stop;
    it->self = self;
    Py_END_CRITICAL_SECTION();

    it->sub = NULL;
    it->vi = vi;
    it->right = right;

    if (bitarray_Check(sub)) {
        Py_INCREF(sub);
        it->sub = (bitarrayobject *) sub;
    }
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(search_doc,
"search(sub_bitarray, start=0, stop=<end>, /, right=False) -> iterator\n\
\n\
Return iterator over indices where sub_bitarray is found, such that\n\
sub_bitarray is contained within `[start:stop]`.\n\
The indices are iterated in ascending order (from lowest to highest),\n\
unless `right=True`, which will iterate in descending order (starting with\n\
rightmost match).");


static PyObject *
searchiter_next(searchiterobject *it)
{
    Py_ssize_t start, stop, pos, width = 1;
    int right;

    Py_BEGIN_CRITICAL_SECTION(it);
    start = it->start;
    stop = it->stop;
    right = it->right;
    Py_END_CRITICAL_SECTION();

    assert(start >= 0);
    if (it->sub) {
        Py_BEGIN_CRITICAL_SECTION2(it->self, it->sub);
        if (start > it->self->nbits || stop < 0 || stop > it->self->nbits) {
            pos = -1;
        }
        else {
            width = it->sub->nbits;
            pos = find_sub(it->self, it->sub, start, stop, right);
        }
        Py_END_CRITICAL_SECTION2();
    }
    else {
        Py_BEGIN_CRITICAL_SECTION(it->self);
        if (start > it->self->nbits || stop < 0 || stop > it->self->nbits) {
            pos = -1;
        }
        else {
            pos = find_bit(it->self, it->vi, start, stop, right);
        }
        Py_END_CRITICAL_SECTION();
    }

    if (pos < 0)  /* no more positions -- stop iteration */
        return NULL;

    /* update start / stop for next iteration */
    Py_BEGIN_CRITICAL_SECTION(it);
    if (right)
        it->stop = pos + width - 1;
    else
        it->start = pos + 1;
    Py_END_CRITICAL_SECTION();

    return PyLong_FromSsize_t(pos);
}

static void
searchiter_dealloc(searchiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->self);
    Py_XDECREF(it->sub);
    PyObject_GC_Del(it);
}

static int
searchiter_traverse(searchiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    Py_VISIT(it->sub);
    return 0;
}

static PyTypeObject SearchIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
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
    {"encode",       (PyCFunction) bitarray_encode,      METH_VARARGS,
     encode_doc},
    {"extend",       (PyCFunction) bitarray_extend,      METH_O,
     extend_doc},
    {"fill",         (PyCFunction) bitarray_fill,        METH_NOARGS,
     fill_doc},
    {"find",         (PyCFunction) bitarray_find,        METH_VARARGS |
                                                         METH_KEYWORDS,
     find_doc},
    {"frombytes",    (PyCFunction) bitarray_frombytes,   METH_O,
     frombytes_doc},
    {"fromfile",     (PyCFunction) bitarray_fromfile,    METH_VARARGS,
     fromfile_doc},
    {"index",        (PyCFunction) bitarray_index,       METH_VARARGS |
                                                         METH_KEYWORDS,
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
    {"rotate",       (PyCFunction) bitarray_rotate,      METH_VARARGS,
     rotate_doc},
    {"search",       (PyCFunction) bitarray_search,      METH_VARARGS |
                                                         METH_KEYWORDS,
     search_doc},
    {"setall",       (PyCFunction) bitarray_setall,      METH_O,
     setall_doc},
    {"sort",         (PyCFunction) bitarray_sort,        METH_VARARGS |
                                                         METH_KEYWORDS,
     sort_doc},
    {"to01",         (PyCFunction) bitarray_to01,        METH_VARARGS |
                                                         METH_KEYWORDS,
     to01_doc},
    {"tobytes",      (PyCFunction) bitarray_tobytes,     METH_NOARGS,
     tobytes_doc},
    {"__bytes__",    (PyCFunction) bitarray_tobytes,     METH_NOARGS,
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

/* Given string 'str', return an integer representing the bit-endianness.
   If the string is invalid, set exception and return -1. */
static int
endian_from_string(const char *str)
{
    if (str == NULL)
        return ENDIAN_DEFAULT;

    if (strcmp(str, "little") == 0)
        return ENDIAN_LITTLE;

    if (strcmp(str, "big") == 0)
        return ENDIAN_BIG;

    PyErr_Format(PyExc_ValueError, "bit-endianness must be either "
                 "'little' or 'big', not '%s'", str);
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

    obj->buffer = PyMem_New(Py_buffer, 1);
    if (obj->buffer == NULL) {
        PyObject_Del(obj);
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }
    memcpy(obj->buffer, &view, sizeof(Py_buffer));

    return (PyObject *) obj;
}

/* return new bitarray of length 'index', 'endian', and
   'init_zero' (initialize buffer with zeros) */
static PyObject *
newbitarray_from_index(PyTypeObject *type, PyObject *index,
                       int endian, int init_zero)
{
    bitarrayobject *res;
    Py_ssize_t nbits;

    assert(PyIndex_Check(index));
    nbits = PyNumber_AsSsize_t(index, PyExc_OverflowError);
    if (nbits == -1 && PyErr_Occurred())
        return NULL;

    if (nbits < 0) {
        PyErr_SetString(PyExc_ValueError, "bitarray length must be >= 0");
        return NULL;
    }

    if ((res = newbitarrayobject(type, nbits, endian)) == NULL)
        return NULL;

    if (init_zero && nbits)
        memset(res->ob_item, 0x00, (size_t) Py_SIZE(res));

    return (PyObject *) res;
}

/* return new bitarray from bytes-like object */
static PyObject *
newbitarray_from_bytes(PyTypeObject *type, PyObject *buffer, int endian)
{
    bitarrayobject *res;
    Py_buffer view;

    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    res = newbitarrayobject(type, 8 * view.len, endian);
    if (res == NULL) {
        PyBuffer_Release(&view);
        return NULL;
    }
    assert(Py_SIZE(res) == view.len);
    if (view.len) {
        Py_BEGIN_CRITICAL_SECTION(buffer);
        memcpy(res->ob_item, (char *) view.buf, (size_t) view.len);
        Py_END_CRITICAL_SECTION();
    }
    PyBuffer_Release(&view);
    return (PyObject *) res;
}

/* As of bitarray version 2.9.0, "bitarray(nbits)" will initialize all items
   to 0 (previously, the buffer was uninitialized).
   However, for speed, one might want to create an uninitialized bitarray.
   In 2.9.1, we added the ability to create uninitialized bitarrays again,
   using "bitarray(nbits, endian, Ellipsis)".
*/
static PyObject *
bitarray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "endian", "buffer", NULL};
    PyObject *initializer = Py_None, *buffer = Py_None;
    bitarrayobject *res;
    char *endian_str = NULL;
    int endian, ret;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OzO:bitarray", kwlist,
                                     &initializer, &endian_str, &buffer))
        return NULL;

    if ((endian = endian_from_string(endian_str)) < 0)
        return NULL;

    /* import buffer */
    if (buffer != Py_None && buffer != Py_Ellipsis) {
        if (initializer != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                            "buffer requires no initializer argument");
            return NULL;
        }
        return newbitarray_from_buffer(type, buffer, endian);
    }

    /* no arg / None */
    if (initializer == Py_None)
        return (PyObject *) newbitarrayobject(type, 0, endian);

    /* bool */
    if (PyBool_Check(initializer)) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot create bitarray from 'bool' object");
        return NULL;
    }

    /* index (a number) */
    if (PyIndex_Check(initializer))
        return newbitarray_from_index(type, initializer, endian,
                                      buffer == Py_None);

    /* bytes or bytearray */
    if (PyBytes_Check(initializer) || PyByteArray_Check(initializer))
        return newbitarray_from_bytes(type, initializer, endian);

    /* bitarray: use its bit-endianness when endian argument is None */
    if (bitarray_Check(initializer) && endian_str == NULL)
        endian = ((bitarrayobject *) initializer)->endian;

    /* empty bitarray to be extended below */
    if ((res = newbitarrayobject(type, 0, endian)) == NULL)
        return NULL;

    if (bitarray_Check(initializer)) {
        Py_BEGIN_CRITICAL_SECTION(initializer);
        ret = extend_bitarray(res, (bitarrayobject *) initializer);
        Py_END_CRITICAL_SECTION();
    }
    else {  /* leave remaining type dispatch to extend method */
        ret = extend_dispatch(res, initializer);
    }

    if (ret < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return (PyObject *) res;
}


static PyObject *
richcompare_lock_held(bitarrayobject *va, bitarrayobject *wa, int op)
{
    Py_ssize_t vs = va->nbits, ws = wa->nbits, i, c;
    char *vb = va->ob_item, *wb = wa->ob_item;

    if (op == Py_EQ || op == Py_NE) {
        /* shortcuts for EQ/NE */
        if (vs != ws) {
            /* if sizes differ, the bitarrays differ */
            return PyBool_FromLong(op == Py_NE);
        }
        else if (va->endian == wa->endian) {
            /* sizes and endianness are the same - use memcmp() */
            int cmp = (vs >= 8) ? memcmp(vb, wb, (size_t) vs / 8) : 0;

            if (cmp == 0 && vs % 8)  /* if equal, compare remaining bits */
                cmp = zlc(va) != zlc(wa);

            return PyBool_FromLong((cmp == 0) ^ (op == Py_NE));
        }
    }

    /* search for the first index where items are different */
    c = Py_MIN(vs, ws) / 8;  /* common buffer size */
    i = 0;                   /* byte index */
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
            Py_RETURN_RICHCOMPARE(vi, wi, op);
    }

    /* no more items to compare -- compare sizes */
    Py_RETURN_RICHCOMPARE(vs, ws, op);
}

static PyObject *
richcompare(PyObject *v, PyObject *w, int op)
{
    PyObject *result;

    if (!bitarray_Check(v) || !bitarray_Check(w))
        return Py_NewRef(Py_NotImplemented);

    Py_BEGIN_CRITICAL_SECTION2(v, w);
    result = richcompare_lock_held((bitarrayobject *) v,
                                   (bitarrayobject *) w, op);
    Py_END_CRITICAL_SECTION2();

    return result;
}

/***************************** bitarray iterator **************************/

typedef struct {
    PyObject_HEAD
    bitarrayobject *self;            /* bitarray we're iterating over */
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
    it->self = self;
    it->index = 0;
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

static PyObject *
bitarrayiter_next(bitarrayiterobject *it)
{
    int vi;

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    if (it->index < it->self->nbits) {
        vi = getbit(it->self, it->index++);
    }
    else {
        vi = -1;  /* stop iteration */
    }
    Py_END_CRITICAL_SECTION2();

    return (vi < 0) ? NULL : PyLong_FromLong(vi);
}

static void
bitarrayiter_dealloc(bitarrayiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->self);
    PyObject_GC_Del(it);
}

static int
bitarrayiter_traverse(bitarrayiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    return 0;
}

static PyTypeObject BitarrayIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
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
   Buffer imports are handled in newbitarray_from_buffer().
*/

static int
bitarray_getbuffer(bitarrayobject *self, Py_buffer *view, int flags)
{
    int ret;

    if (view == NULL) {
        Py_BEGIN_CRITICAL_SECTION(self);
        self->ob_exports++;
        Py_END_CRITICAL_SECTION();
        return 0;
    }

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = PyBuffer_FillInfo(view,
                            (PyObject *) self,  /* exporter */
                            (void *) self->ob_item,
                            Py_SIZE(self),
                            self->readonly,
                            flags);
    if (ret >= 0)
        self->ob_exports++;

    Py_END_CRITICAL_SECTION();

    return ret;
}

static void
bitarray_releasebuffer(bitarrayobject *self, Py_buffer *view)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    self->ob_exports--;
    Py_END_CRITICAL_SECTION();
}

static PyBufferProcs bitarray_as_buffer = {
    (getbufferproc) bitarray_getbuffer,
    (releasebufferproc) bitarray_releasebuffer,
};

/***************************** Bitarray Type ******************************/

PyDoc_STRVAR(bitarraytype_doc,
"bitarray(initializer=0, /, endian='big', buffer=None) -> bitarray\n\
\n\
Return a new bitarray object whose items are bits initialized from\n\
the optional initializer, and bit-endianness.\n\
The initializer may be one of the following types:\n\
a.) `int` bitarray, initialized to zeros, of given length\n\
b.) `bytes` or `bytearray` to initialize buffer directly\n\
c.) `str` of 0s and 1s, ignoring whitespace and \"_\"\n\
d.) iterable of integers 0 or 1.\n\
\n\
Optional keyword arguments:\n\
\n\
`endian`: Specifies the bit-endianness of the created bitarray object.\n\
Allowed values are `big` and `little` (the default is `big`).\n\
The bit-endianness affects the buffer representation of the bitarray.\n\
\n\
`buffer`: Any object which exposes a buffer.  When provided, `initializer`\n\
cannot be present (or has to be `None`).  The imported buffer may be\n\
read-only or writable, depending on the object type.");


static PyTypeObject Bitarray_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
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
bits2bytes(PyObject *module, PyObject *n)
{
    PyObject *zero, *seven, *eight, *a, *b;
    int cmp_res;

    if (!PyLong_Check(n))
        return PyErr_Format(PyExc_TypeError, "'int' object expected, "
                            "got '%s'", Py_TYPE(n)->tp_name);

    if ((zero = Py_GetConstant(Py_CONSTANT_ZERO)) == NULL)
        return NULL;
    cmp_res = PyObject_RichCompareBool(n, zero, Py_LT);
    Py_DECREF(zero);

    if (cmp_res < 0)
        return NULL;
    if (cmp_res) {
        PyErr_SetString(PyExc_ValueError, "non-negative int expected");
        return NULL;
    }

    if ((seven = PyLong_FromLong(7)) == NULL)
        return NULL;
    a = PyNumber_Add(n, seven);          /* a = n + 7 */
    Py_DECREF(seven);
    if (a == NULL)
        return NULL;

    if ((eight = PyLong_FromLong(8)) == NULL) {
        Py_DECREF(a);
        return NULL;
    }
    b = PyNumber_FloorDivide(a, eight);  /* b = a // 8 */
    Py_DECREF(eight);
    Py_DECREF(a);

    return b;
}

PyDoc_STRVAR(bits2bytes_doc,
"bits2bytes(n, /) -> int\n\
\n\
Return the number of bytes necessary to store n bits.");


static PyObject *
reconstructor(PyObject *module, PyObject *args)
{
    PyTypeObject *type;
    Py_ssize_t nbytes;
    PyObject *bytes;
    bitarrayobject *res;
    char *endian_str;
    int endian, padbits, readonly;

    if (!PyArg_ParseTuple(args, "OOsii:_bitarray_reconstructor",
                          &type, &bytes, &endian_str, &padbits, &readonly))
        return NULL;

    if (!PyType_Check(type))
        return PyErr_Format(PyExc_TypeError, "first argument must be a type "
                            "object, got '%s'", Py_TYPE(type)->tp_name);

    if (!PyType_IsSubtype(type, &Bitarray_Type))
        return PyErr_Format(PyExc_TypeError, "'%s' is not a subtype of "
                            "bitarray", type->tp_name);

    if (!PyBytes_Check(bytes))
        return PyErr_Format(PyExc_TypeError, "second argument must be bytes, "
                            "got '%s'", Py_TYPE(bytes)->tp_name);

    if ((endian = endian_from_string(endian_str)) < 0)
        return NULL;

    nbytes = PyBytes_GET_SIZE(bytes);
    if (padbits < 0 || padbits > 7 || (nbytes == 0 && padbits))
        return PyErr_Format(PyExc_ValueError,
                            "invalid number of pad bits: %d", padbits);

    res = newbitarrayobject(type, 8 * nbytes - padbits, endian);
    if (res == NULL)
        return NULL;
    assert(Py_SIZE(res) == nbytes);
    if (nbytes)
        memcpy(res->ob_item, PyBytes_AS_STRING(bytes), (size_t) nbytes);
    if (readonly) {
        set_padbits(res);
        res->readonly = 1;
    }
    return (PyObject *) res;
}


static PyObject *
get_default_endian(PyObject *module)
{
    return PyUnicode_FromString(ENDIAN_STR(ENDIAN_DEFAULT));
}

PyDoc_STRVAR(get_default_endian_doc,
"get_default_endian() -> str\n\
\n\
Return the default bit-endianness for new bitarray objects being created.");


static PyObject *
sysinfo(PyObject *module, PyObject *args)
{
    char *key;

    if (!PyArg_ParseTuple(args, "s:_sysinfo", &key))
        return NULL;

#define R(k, v)                             \
    if (strcmp(key, k) == 0)                \
        return PyLong_FromLong((long) (v))

    R("void*", sizeof(void *));
    R("size_t", sizeof(size_t));
    R("bitarrayobject", sizeof(bitarrayobject));
    R("decodetreeobject", sizeof(decodetreeobject));
    R("binode", sizeof(binode));
    R("PY_LITTLE_ENDIAN", PY_LITTLE_ENDIAN);
    R("PY_BIG_ENDIAN", PY_BIG_ENDIAN);
    R("HAVE_BUILTIN_BSWAP64", HAVE_BUILTIN_BSWAP64);
#ifdef Py_GIL_DISABLED   /* Python configured using --disable-gil */
    R("Py_GIL_DISABLED", 1);
#else
    R("Py_GIL_DISABLED", 0);
#endif
#ifdef Py_DEBUG          /* Python configured using --with-pydebug  */
    R("Py_DEBUG", 1);
#else
    R("Py_DEBUG", 0);
#endif
#ifndef NDEBUG           /* bitarray compiled without -DNDEBUG */
    R("DEBUG", 1);
#else
    R("DEBUG", 0);
#endif

    PyErr_SetString(PyExc_KeyError, key);
    return NULL;
#undef R
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo(key) -> int\n\
\n\
Return system- and compile-specific information given a key.");


static PyMethodDef module_functions[] = {
    {"bits2bytes",          (PyCFunction) bits2bytes,         METH_O,
     bits2bytes_doc},
    {"_bitarray_reconstructor",
                            (PyCFunction) reconstructor,      METH_VARARGS,
     reduce_doc},
    {"get_default_endian",  (PyCFunction) get_default_endian, METH_NOARGS,
     get_default_endian_doc},
    {"_sysinfo",            (PyCFunction) sysinfo,            METH_VARARGS,
     sysinfo_doc},
    {NULL,                  NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

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

PyMODINIT_FUNC
PyInit__bitarray(void)
{
    PyObject *m;

    /* setup translation table, which maps each byte to its reversed:
       reverse_trans = {0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, ..., 0xff} */
    setup_table(reverse_trans, 'r');

    if ((m = PyModule_Create(&moduledef)) == NULL)
        return NULL;

#ifdef Py_GIL_DISABLED
    PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED);
#endif

    if (PyType_Ready(&Bitarray_Type) < 0)
        return NULL;
    Py_SET_TYPE(&Bitarray_Type, &PyType_Type);
    Py_INCREF((PyObject *) &Bitarray_Type);
    PyModule_AddObject(m, "bitarray", (PyObject *) &Bitarray_Type);

    if (register_abc() < 0)
        return NULL;

    if (PyType_Ready(&DecodeTree_Type) < 0)
        return NULL;
    Py_SET_TYPE(&DecodeTree_Type, &PyType_Type);
    Py_INCREF((PyObject *) &DecodeTree_Type);
    PyModule_AddObject(m, "decodetree", (PyObject *) &DecodeTree_Type);

    if (PyType_Ready(&DecodeIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&DecodeIter_Type, &PyType_Type);
    Py_INCREF((PyObject *) &DecodeIter_Type);
    PyModule_AddObject(m, "decodeiterator", (PyObject *) &DecodeIter_Type);

    if (PyType_Ready(&BitarrayIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&BitarrayIter_Type, &PyType_Type);

    if (PyType_Ready(&SearchIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&SearchIter_Type, &PyType_Type);

    if (PyModule_AddStringMacro(m, BITARRAY_VERSION) < 0)
        return NULL;

    return m;
}
