# issue 6
# http://www-graphics.stanford.edu/~seander/bithacks.html#NextBitPermutation

from bitarray import bitarray, get_default_endian
from bitarray.util import ba2int, int2ba, zeros

from math import comb


def all_perm(n, k, endian=None):
    """all_perm(n, k, endian=None) -> iterator

Return an iterator over all bitarrays of length `n` with `k` bits set to 1
in lexicographical order.
"""
    n = int(n)
    if n < 0:
        raise ValueError("length must be >= 0")
    k = int(k)
    if k < 0 or k > n:
        raise ValueError("number of set bits must be in range(0, n + 1)")

    if k == 0:
        yield zeros(n, endian)
        return

    v = (1 << k) - 1
    for _ in range(comb(n, k)):
        yield int2ba(v, length=n,
                endian=get_default_endian() if endian is None else endian)
        t = (v | (v - 1)) + 1
        v = t | ((((t & -t) // (v & -v)) >> 1) - 1)


def next_perm(a):
    """next_perm(bitarray) -> bitarray

Return the next lexicographical permutation.  The length and the number
of 1 bits in the bitarray is unchanged.  The integer value (`ba2int`) of the
next permutation will always increase, except when the cycle is completed (in
which case the lowest lexicographical permutation will be returned).
"""
    v = ba2int(a)
    if v == 0:
        return a
    t = (v | (v - 1)) + 1
    w = t | ((((t & -t) // (v & -v)) >> 1) - 1)
    try:
        return int2ba(w, length=len(a), endian=a.endian())
    except OverflowError:
        return a[::-1]

# ---------------------------------------------------------------------------

import unittest

from bitarray import frozenbitarray
from bitarray.util import urandom
from bitarray.test_bitarray import Util


class PermTests(unittest.TestCase, Util):

    def test_explicit_1(self):
        a = bitarray('00010011', 'big')
        for s in ['00010101', '00010110', '00011001',
                  '00011010', '00011100', '00100011']:
            a = next_perm(a)
            self.assertEqual(a.count(), 3)
            self.assertEqual(a, bitarray(s, 'big'))

    def test_explicit_2(self):
        for seq in (['0'], ['1'], ['00'], ['11'], ['01', '10'],
                    ['001', '010', '100'], ['011', '101', '110'],
                    ['0011', '0101', '0110', '1001', '1010', '1100']):
            a = bitarray(seq[0], 'big')
            for i in range(20):
                self.assertEqual(a, bitarray(seq[i % len(seq)]))
                a = next_perm(a)

    def test_all_same(self):
        for endian in 'little', 'big':
            for n in range(1, 30):
                for v in 0, 1:
                    a = bitarray(n, endian)
                    a.setall(v)
                    self.assertEqual(next_perm(a), a)

    def test_turnover(self):
        for a in [bitarray('11111110000', 'big'),
                  bitarray('0000001111111', 'little')]:
            self.assertEqual(next_perm(a), a[::-1])

    def test_large(self):
        a = bitarray('10010101010100100110010101110100111100101111', 'big')
        b = next_perm(a)
        c = bitarray('10010101010100100110010101110100111100110111')
        self.assertEqual(b, c)

    def test_errors(self):
        self.assertRaises(ValueError, next_perm, bitarray())
        self.assertRaises(TypeError, next_perm, '1')

    def check_all_perm(self, s):
        s1 = s.count(1)
        n = 0
        a = bitarray(s)
        coll = set()
        while 1:
            a = next_perm(a)
            coll.add(frozenbitarray(a))
            self.assertEqual(len(a), len(s))
            self.assertEqual(a.count(), s1)
            self.assertEqual(a.endian(), s.endian())
            n += 1
            if a == s:
                break
        self.assertEqual(n, comb(len(s), s1))
        self.assertEqual(len(coll), n)

    def check_order(self, a):
        i = -1
        for _ in range(comb(len(a), a.count())):
            i, j = ba2int(a), i
            self.assertTrue(i > j)
            a = next_perm(a)

    def test_few(self):
        for s in '0', '1', '00', '01', '111', '0011', '01011', '000000011':
            for endian in 'little', 'big':
                a = bitarray(s, endian)
                self.check_all_perm(a)
                a.sort(a.endian() == 'little')
                self.check_order(a)

    def test_random(self):
        for n in range(1, 10):
            a = urandom(n, self.random_endian())
            self.check_all_perm(a)
            a.sort(a.endian() == 'little')
            self.check_order(a)

    def test_all_perm_explicit(self):
        for n, k, res in [
                (0, 0, ['']),
                (1, 0, ['0']),
                (1, 1, ['1']),
                (2, 0, ['00']),
                (2, 1, ['01', '10']),
                (3, 2, ['011', '101', '110']),
                ]:
            self.assertEqual(list(all_perm(n, k, 'big')),
                             [bitarray(s) for s in res])

    def test_all_perm_1(self):
        n, k = 10, 5
        c = 0
        s = set()
        for a in all_perm(n, k, 'little'):
            self.assertIsType(a, 'bitarray')
            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), k)
            s.add(frozenbitarray(a))
            c += 1
        self.assertEqual(c, comb(n, k))
        self.assertEqual(len(s), comb(n, k))

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
