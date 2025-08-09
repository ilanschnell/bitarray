# issue 6
# http://www-graphics.stanford.edu/~seander/bithacks.html#NextBitPermutation

from bitarray import bitarray
from bitarray.util import zeros, ba2int, int2ba


def all_perm(n, k, endian=None):
    """all_perm(n, k, endian=None) -> iterator

Return an iterator over all bitarrays of length `n` with `k` bits set to one
in lexicographical order.
"""
    if n < 0:
        raise ValueError("length must be >= 0")

    # error check inputs and handle edge cases
    if k <= 0 or k > n:
        if k == 0:
            yield zeros(n, endian)
            return
        raise ValueError("k must be in range 0 <= k <= n, got %s" % k)

    v = (1 << k) - 1
    while True:
        try:
            yield int2ba(v, length=n, endian=endian)
        except OverflowError:
            return
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
    v = t | ((((t & -t) // (v & -v)) >> 1) - 1)
    try:
        return int2ba(v, length=len(a), endian=a.endian)
    except OverflowError:
        return a[::-1]

# ---------------------------------------------------------------------------

import unittest
from math import comb
from random import choice, getrandbits, randrange
from itertools import pairwise

from bitarray import frozenbitarray
from bitarray.util import random_k


class PermTests(unittest.TestCase):

    def test_explicit_1(self):
        a = bitarray('00010011', 'big')
        for s in ['00010101', '00010110', '00011001',
                  '00011010', '00011100', '00100011']:
            a = next_perm(a)
            self.assertEqual(a.count(), 3)
            self.assertEqual(a, bitarray(s, 'big'))

    def test_zeros_ones(self):
        for n in range(1, 30):
            endian = choice(["little", "big"])
            v = getrandbits(1)

            lst = list(all_perm(n, v * n, endian))
            self.assertEqual(len(lst), 1)
            a = lst[0]
            c = a.copy()
            self.assertEqual(a.endian, endian)
            self.assertEqual(len(a), n)
            if v:
                self.assertTrue(a.all())
            else:
                self.assertFalse(a.any())
            self.assertEqual(next_perm(a), a)
            self.assertEqual(a, c)

    def test_turnover(self):
        for a in [bitarray('11111110000', 'big'),
                  bitarray('0000001111111', 'little')]:
            self.assertEqual(next_perm(a), a[::-1])

    def test_next_perm_random(self):
        for _ in range(100):
            n = randrange(2, 1_000_000)
            k = randrange(1, n)
            a = random_k(n, k, endian=choice(["little", "big"]))
            b = next_perm(a)
            self.assertEqual(len(b), n)
            self.assertEqual(b.count(), k)
            self.assertEqual(b.endian, a.endian)
            self.assertNotEqual(a, b)
            if ba2int(a) > ba2int(b):
                c = a.copy()
                c.sort(c.endian == 'big')
                self.assertEqual(a, c)
                self.assertEqual(b, a[::-1])

    def test_errors(self):
        self.assertRaises(ValueError, next_perm, bitarray())
        self.assertRaises(TypeError, next_perm, '1')

    def check_perm_cycle(self, start):
        n, k = len(start), start.count()
        a = bitarray(start)
        coll = set()
        c = 0
        while True:
            a = next_perm(a)
            coll.add(frozenbitarray(a))
            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), k)
            self.assertEqual(a.endian, start.endian)
            c += 1
            if a == start:
                break
        self.assertEqual(c, comb(n, k))
        self.assertEqual(len(coll), c)

    def test_all_perm_explicit(self):
        for n, k, res in [
                (0, 0, ['']),
                (1, 0, ['0']),
                (1, 1, ['1']),
                (2, 0, ['00']),
                (2, 1, ['01', '10']),
                (2, 2, ['11']),
                (3, 0, ['000']),
                (3, 1, ['001', '010', '100']),
                (3, 2, ['011', '101', '110']),
                (3, 3, ['111']),
                (4, 2, ['0011', '0101', '0110', '1001', '1010', '1100']),
        ]:
            lst = list(all_perm(n, k, 'big'))
            self.assertEqual(len(lst), comb(n, k))
            self.assertEqual(lst, [bitarray(s) for s in res])
            if n == 0:
                continue
            a = lst[0]
            for i in range(20):
                self.assertEqual(a, bitarray(res[i % len(lst)]))
                a = next_perm(a)

    def test_all_perm(self):
        n, k = 17, 5
        endian=choice(["little", "big"])

        prev = None
        cnt = 0
        coll = set()
        for a in all_perm(n, k, endian):
            self.assertEqual(type(a), bitarray)
            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), k)
            self.assertEqual(a.endian, endian)
            coll.add(frozenbitarray(a))
            if prev is None:
                first = a.copy()
                c = a.copy()
                c.sort(c.endian == "little")
                self.assertEqual(a, c)
            else:
                self.assertEqual(next_perm(prev), a)
                self.assertTrue(ba2int(prev) < ba2int(a))
            prev = a
            cnt += 1

        self.assertEqual(cnt, comb(n, k))
        self.assertEqual(len(coll), cnt)

        # a is now the last permutation
        last = a.copy()
        self.assertTrue(ba2int(first) < ba2int(last))
        self.assertEqual(last, first[::-1])

    def test_all_perm_order(self):
        n, k = 10, 5
        for a, b in pairwise(all_perm(n, k, 'little')):
            self.assertTrue(ba2int(b) > ba2int(a))
            self.assertEqual(next_perm(a), b)


if __name__ == '__main__':
    unittest.main()
