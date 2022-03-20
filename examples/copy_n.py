"""
The purpose of this script is to illustrate how copy_n() in _bitarray.c works.
This is essentially a Python implementation of copy_n() with output of the
different stages of the bitarray we copy into.
For more details, see also: bitarray/copy_n.txt
"""
from __future__ import print_function

import sys
from random import randint
if sys.version_info[0] == 2:
    from io import BytesIO as StringIO
else:
    from io import StringIO

from bitarray import bitarray
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
    assert a <= b and 0 <= n < 8
    self[8 * a : 8 * b] >>= n


def copy_n(self, a, other, b, n):
    assert 0 <= a <= len(self)
    assert 0 <= b <= len(other)
    assert n >= 0
    if n == 0 or (self == other and a == b):
        return

    if a % 8 == 0 and b % 8 == 0 and n >= 8: # aligned case
        m = n // 8

        if a > b:
            copy_n(self, a + 8 * m, other, b + 8 * m, n % 8)

        memoryview(self)[a//8:a//8 + m] = memoryview(other)[b//8:b//8 + m]
        if self.endian() != other.endian():
            self.bytereverse(a//8, a//8 + m)

        if a <= b:
            copy_n(self, a + 8 * m, other, b + 8 * m, n % 8)
        return

    if n < 24:                               # small n case
        if a <= b:  # loop forward
            for i in range(n):
                self[i + a] = other[i + b]
        else:       # loop backwards
            for i in range(n - 1, -1, -1):
                self[i + a] = other[i + b]
        return

    # -------------------------------------- # general case
    p1 = a // 8
    p2 = (a + n - 1) // 8
    p3 = b // 8
    sa = a % 8
    sb = -(b % 8)

    assert n >= 8
    assert a - sa == 8 * p1
    assert b + sb == 8 * p3
    assert a + n >= 8 * p2

    if verbose:
        print('a =', a)
        print('b =', b)
        print('n =', n)
        print('p1 =', p1)
        print('p2 =', p2)
        print('p3 =', p3)
        print('sa =', sa)
        print('sb =', sb)

    t1 = self[8 * p1: 8 * p1 + 8]
    t2 = self[8 * p2: 8 * p2 + 8]
    t3 = other[8 * p3: 8 * p3 + 8]

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
        mark_range_n(8 * p1, n - sb, '=')

    copy_n(self, 8 * p1, other, b + sb, n - sb)
    if verbose:
        pprint(self)

        print('rshift', sa + sb)
        mark_range(8 * p1, 8 * (p2 + 1), '>')

    shift_r8(self, p1, p2 + 1, sa + sb)
    if verbose:
        pprint(self)
        mark_range_n(8 * p1 + sa + sb, n - sb, '=', 'a..a+n')

    if verbose:
        mark_range(8 * p1, a, '1')
    for i in range(8 * p1, a):
        self[i] = t1[i % 8]

    if sa + sb != 0:
        if verbose:
            mark_range(a + n, 8 * p2 + 8, '2')
        for i in range(a + n, min(8 * p2 + 8, len(self))):
            self[i] = t2[i % 8]

    if verbose:
        mark_range_n(a, sb, '3')
    for i in range(0, sb):
        self[i + a] = t3[(i + b) % 8]

    if verbose:
        pprint(self)


def test_copy_n():

    def random_endian():
        return ['little', 'big'][randint(0, 1)]

    for N in range(1000):
        M = randint(0, 5 + 2 * N)
        n = randint(0, min(N, M))
        a = randint(0, N - n)
        b = randint(0, M - n)
        x = urandom(N, random_endian())
        y = urandom(M, random_endian())
        z = x.copy()
        copy_n(x, a, y, b, n)
        z[a:a + n] = y[b:b + n]
        assert x == z

    for N in range(1000):
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
