"""
The purpose of this script is to illustrate how copy_n() in _bitarray.c works.
This is a Python implementation of copy_n() with output of the different
stages of the bitarray we copy into.

Sample output:
a = 21
b = 6
n = 31
p1 = 2
p2 = 6
p3 = 0
sa = 5
sb = -6
 -> p3 = 1
 -> sb = 2
other
bitarray('00101110 11111001 01011101 11001011 10110000 01011110 011')
b..b+n          ^^ ^^^^^^^^ ^^^^^^^^ ^^^^^^^^ ^^^^^
                   ======== ======== ======== ========
                33
self
bitarray('01011101 11100101 01110101 01011001 01110100 10001010 01111011')
a..a+n                           ^^^ ^^^^^^^^ ^^^^^^^^ ^^^^^^^^ ^^^^
                            11111
                                                                    2222
memmove 4
                            ======== ======== ======== ========
bitarray('01011101 11100101 11111001 01011101 11001011 10110000 01111011')
rshift 7
                            >>>>>>>> >>>>>>>> >>>>>>>> >>>>>>>> >>>>>>>>
bitarray('01011101 11100101 00000001 11110010 10111011 10010111 01100000')
                                   = ======== ======== ======== ========
                            11111
                                                                    2222
                                 33
bitarray('01011101 11100101 01110101 11110010 10111011 10010111 01101011')
"""
from io import StringIO

from bitarray import bitarray, bits2bytes
from bitarray.util import pprint


verbose = False

def mark_range_n(i, n, c, text=''):
    a = bitarray(i * '0' + n * '1')
    f = StringIO()
    pprint(a, stream=f)
    s = f.getvalue()
    print("%-10s" % text + ''.join(c if e == '1' else ' ' for e in s[10:]))


def mark_range(i, j, c, text=''):
    mark_range_n(i, j - i, c, text)


def shift_r8(self, a, b, k):
    """
    shift bits in byte-range(a, b) by k bits to right (in-place)
    """
    assert 0 <= k < 8
    assert 0 <= a <= self.nbytes
    assert 0 <= b <= self.nbytes
    if k == 0 or a >= b:
        return
    self[8 * a : 8 * b] >>= k

def is_be(self):
    return self.endian == 'big'

bitmask_table = [
    [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80],  # little endian
    [0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01],  # big endian
]

ones_table = [
    [0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f],  # little endian
    [0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe],  # big endian
]

def copy_n(self, a, other, b, n):
    """
    copy n bits from other (starting at b) onto self (starting at a)
    """
    p1 = a // 8               # first byte to be copied to
    p2 = (a + n - 1) // 8     # last byte to be copied to
    p3 = b // 8               # first byte to be memmoved from
    sa = a % 8
    sb = -(b % 8)
    t3 = 0

    if verbose:
        print('a =', a)
        print('b =', b)
        print('n =', n)
        print('p1 =', p1)
        print('p2 =', p2)
        print('p3 =', p3)
        print('sa =', sa)
        print('sb =', sb)

    assert 0 <= n <= min(len(self), len(other))
    assert 0 <= a <= len(self) - n
    assert 0 <= b <= len(other) - n
    if n == 0 or (self is other and a == b):
        return

    if sa + sb < 0:
        # In order to keep total right shift (sa + sb) positive, we
        # increase the first byte to be copied from (p3) by one byte,
        # such that memmove() will move all bytes one extra to the left.

        # As other may be self, we need to store this byte as its memory
        # location may be overwritten or changed by memmove or rshift.
        t3 = memoryview(other)[p3]
        p3 += 1
        sb += 8
        if verbose:
            print(' -> p3 =', p3)
            print(' -> sb =', sb)

    assert a - sa == 8 * p1
    assert b + sb == 8 * p3
    assert p1 <= p2
    assert 8 * p2 < a + n <= 8 * (p2 + 1)

    if verbose:
        print('other')
        pprint(other)
        mark_range_n(b, n, '^', 'b..b+n')
        if n > sb:
            mark_range_n(8 * p3, 8 * bits2bytes(n - sb), '=')
        mark_range_n(b, sb, '3')
        print('self')
        pprint(self)
        mark_range_n(a, n, '^', 'a..a+n')
        if n > sb:
            mark_range(8 * p1, a, '1')
            mark_range(a + n, 8 * p2 + 8, '2')

    if n > sb:
        m = bits2bytes(n - sb)             # number of bytes memmoved
        table = ones_table[is_be(self)]
        m1 = table[sa]
        m2 = table[(a + n) % 8]
        t1 = memoryview(self)[p1]
        t2 = memoryview(self)[p2]

        assert p1 + m in [p2, p2 + 1]
        assert p1 + m <= self.nbytes and p3 + m <= other.nbytes

        # aligned copy -- copy first sb bits (if any) later
        memoryview(self)[p1:p1 + m] = memoryview(other)[p3:p3 + m]
        if self.endian != other.endian:
            self.bytereverse(p1, p1 + m)

        if verbose:
            print('memmove', m)
            mark_range_n(8 * p1, 8 * m, '=')
            pprint(self)
            print('rshift', sa + sb)
            mark_range(8 * p1, 8 * (p2 + 1), '>')

        shift_r8(self, p1, p2 + 1, sa + sb)          # right shift
        if verbose:
            pprint(self)
            mark_range(8 * p1 + sa + sb, 8 * (p2 + 1), '=')

        if m1:               # restore bits at p1
            if verbose:
                mark_range(8 * p1, a, '1')
            memoryview(self)[p1] = (memoryview(self)[p1] & ~m1) | (t1 & m1)

        if m2:               # restore bits at p2
            if verbose:
                mark_range(a + n, 8 * p2 + 8, '2')
            memoryview(self)[p2] = (memoryview(self)[p2] & m2) | (t2 & ~m2)

    if verbose:
        mark_range_n(a, sb, '3')
    for i in range(min(sb, n)):  # copy first sb bits
        self[i + a] = bool(t3 & bitmask_table[is_be(other)][(i + b) % 8])

    if verbose:
        pprint(self)


def test_copy_n():
    from random import choice, randrange, randint
    from bitarray.util import urandom

    def random_endian():
        return choice(['little', 'big'])

    max_size = 56

    for _ in range(10_000):
        N = randrange(max_size)
        M = randrange(max_size)
        n = randint(0, min(N, M))
        a = randint(0, N - n)
        b = randint(0, M - n)
        x = urandom(N, random_endian())
        y = urandom(M, random_endian())
        z = x.copy()
        copy_n(x, a, y, b, n)
        z[a:a + n] = y[b:b + n]
        assert x == z

    for _ in range(10_000):
        N = randrange(max_size)
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
    #copy_n(self, 2, other, 12, 1)
    #copy_n(self, 9, other, 17, 23)
