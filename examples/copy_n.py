"""
The purpose of this script is to illustrate how copy_n() in _bitarray.c works.
This is essentially a Python implementation of copy_n() with output of the
different stages of the bitarray we copy into.
For more details, see also: bitarray/copy_n.txt
"""
from random import getrandbits, randrange, randint
from io import StringIO

from bitarray import bitarray, bits2bytes
from bitarray.util import pprint, urandom


verbose = False

def mark_range_n(i, n, c, text=''):
    a = bitarray(i * '0' + n * '1')
    f = StringIO()
    pprint(a, stream=f)
    s = f.getvalue()
    print("%-10s" % text + ''.join(c if e == '1' else ' ' for e in s[10:]))


def mark_range(i, j, c, text=''):
    mark_range_n(i, j - i, c, text)


def shift_r8(self, a, b, n):
    assert 0 <= n < 8
    assert 0 <= a <= self.nbytes
    assert 0 <= b <= self.nbytes
    if n == 0 or a >= b:
        return
    self[8 * a : 8 * b] >>= n

def is_be(self):
    return self.endian() == 'big'

bitmask_table = [
    [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80],  # little endian
    [0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01],  # big endian
]

ones_table = [
    [0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f],  # little endian
    [0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe],  # big endian
]

def copy_n(self, a, other, b, n):
    assert 0 <= n <= min(len(self), len(other))
    assert 0 <= a <= len(self) - n
    assert 0 <= b <= len(other) - n
    if n == 0 or (self is other and a == b):
        return

    if a % 8 == 0 and b % 8 == 0:            # aligned case
        p1 = a // 8
        p2 = (a + n - 1) // 8
        m = bits2bytes(n)

        assert p1 + m == p2 + 1
        m2 = ones_table[is_be(self)][(a + n) % 8]
        t2 = memoryview(self)[p2]

        memoryview(self)[p1:p1 + m] = memoryview(other)[b // 8:b // 8 + m]
        if self.endian() != other.endian():
            self.bytereverse(p1, p2 + 1)

        if m2:  # restore bits overwritten by highest copied byte
            memoryview(self)[p2] = (memoryview(self)[p2] & m2) | (t2 & ~m2)

    elif n < 8:                              # small n case
        if a <= b:  # loop forward (delete)
            for i in range(n):
                self[i + a] = other[i + b]
        else:       # loop backwards (insert)
            for i in range(n - 1, -1, -1):
                self[i + a] = other[i + b]

    else:                                    # general case
        p1 = a // 8
        p2 = (a + n - 1) // 8
        p3 = b // 8
        sa = a % 8
        sb = -(b % 8)
        m1 = ones_table[is_be(self)][sa]
        m2 = ones_table[is_be(self)][(a + n) % 8]

        assert n >= 8 and p1 <= p2
        assert a - sa == 8 * p1
        assert b + sb == 8 * p3
        assert a + n > 8 * p2

        if verbose:
            print('a =', a)
            print('b =', b)
            print('n =', n)
            print('p1 =', p1)
            print('p2 =', p2)
            print('p3 =', p3)
            print('sa =', sa)
            print('sb =', sb)

        t1 = memoryview(self)[p1]
        t2 = memoryview(self)[p2]
        t3 = memoryview(other)[p3]

        if sa + sb < 0:
            sb += 8
            if verbose:
                print(' -> sb =', sb)

        if verbose:
            print('other')
            pprint(other)
            mark_range_n(b, n, '^', 'b..b+n')
            mark_range_n(b + sb, n - sb, '=')
            mark_range_n(b, sb, '3')

            print('self')
            pprint(self)
            mark_range_n(a, n, '^', 'a..a+n')
            mark_range(8 * p1, a, '1')
            mark_range(a + n, 8 * p2 + 8, '2')

            print('copy_n')
            mark_range_n(a - sa, n - sb, '=')

        copy_n(self, a - sa, other, b + sb, n - sb)  # aligned copy
        if verbose:
            pprint(self)

            print('rshift', sa + sb)
            mark_range(8 * p1, 8 * (p2 + 1), '>')

        shift_r8(self, p1, p2 + 1, sa + sb)          # right shift
        if verbose:
            pprint(self)
            mark_range_n(8 * p1 + sa + sb, n - sb, '=', 'a..a+n')

        if m1:               # restore bits at p1
            if verbose:
                mark_range(8 * p1, a, '1')
            memoryview(self)[p1] = (memoryview(self)[p1] & ~m1) | (t1 & m1)

        if m2 and sa + sb:   # if shifted, restore bits at p2
            if verbose:
                mark_range(a + n, 8 * p2 + 8, '2')
            memoryview(self)[p2] = (memoryview(self)[p2] & m2) | (t2 & ~m2)

        if verbose:
            mark_range_n(a, sb, '3')
        for i in range(sb):  # copy first bits missed by copy_n()
            self[i + a] = bool(t3 & bitmask_table[is_be(other)][(i + b) % 8])

        if verbose:
            pprint(self)


def test_copy_n():

    def random_endian():
        return ['little', 'big'][getrandbits(1)]

    for _ in range(10000):
        N = randrange(200)
        M = randrange(200)
        n = randint(0, min(N, M))
        a = randint(0, N - n)
        b = randint(0, M - n)
        x = urandom(N, random_endian())
        y = urandom(M, random_endian())
        z = x.copy()
        copy_n(x, a, y, b, n)
        z[a:a + n] = y[b:b + n]
        assert x == z

    for _ in range(10000):
        N = randrange(200)
        n = randint(0, N)
        a = randint(0, N - n)
        b = randint(0, N - n)
        x = urandom(N, random_endian())
        z = x.copy()
        copy_n(x, a, x, b, n)
        z[a:a + n] = z[b:b + n]
        assert x == z


if __name__ == '__main__':
    test_copy_n()
    verbose = True
    other = bitarray(
        '00101110 11111001 01011101 11001011 10110000 01011110 011')
    self =  bitarray(
        '01011101 11100101 01110101 01011001 01110100 10001010 01111011')
    copy_n(self, 21, other, 6, 31)
    assert self == bitarray(
        '01011101 11100101 01110101 11110010 10111011 10010111 01101011')
