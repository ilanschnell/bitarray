# issue 6
# http://www-graphics.stanford.edu/~seander/bithacks.html#NextBitPermutation

from bitarray import bitarray
from bitarray.util import zeros, ba2int, int2ba


def lexico_all(n, k, endian=None):
    """lexico_all(n, k, endian=None) -> iterator

Return an iterator over all bitarrays of length `n` and
population count `k` in lexicographical order.
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


def lexico_next(__a):
    """lexico_next(a, /) -> bitarray

Return the next lexicographical permutation of bitarray `a`.
The length and population count of the result remains unchanged.
The integer value (`ba2int()`) of the next permutation will always increase,
except when the cycle is completed.  In that case, the lowest lexicographical
permutation will be returned.
"""
    if not __a:
        return __a

    v = ba2int(__a)
    if v == 0:
        return __a

    t = (v | (v - 1)) + 1
    v = t | ((((t & -t) // (v & -v)) >> 1) - 1)
    try:
        return int2ba(v, length=len(__a), endian=__a.endian)
    except OverflowError:
        return __a[::-1]

# ---------------------- lexicographical permutations -----------------------

import math
import unittest
from random import choice, getrandbits, randrange
from itertools import pairwise

from bitarray import frozenbitarray
from bitarray.util import random_k


class LexicoTests(unittest.TestCase):

    def test_errors(self):
        N = lexico_next
        self.assertRaises(TypeError, N)
        self.assertRaises(TypeError, N, bitarray('1'), 1)
        self.assertRaises(TypeError, N, '1')

        A = lexico_all
        self.assertRaises(TypeError, A)
        self.assertRaises(TypeError, A, 4)
        self.assertRaises(TypeError, next, A("4", 2))
        self.assertRaises(TypeError, next, A(1, "0.5"))
        self.assertRaises(TypeError, A, 1, p=1)
        self.assertRaises(TypeError, next, A(11, 5.5))
        self.assertRaises(ValueError, next, A(-1, 0))
        for k in -1, 11:  # k is not 0 <= k <= n
            self.assertRaises(ValueError, next, A(10, k))
        self.assertRaises(ValueError, next, A(10, 7, 'foo'))
        self.assertRaises(ValueError, next, A(10, 7, endian='foo'))

    def test_zeros_ones(self):
        for n in range(30):
            endian = choice(["little", "big"])
            a = bitarray(n, endian)
            a.setall(getrandbits(1))
            b = lexico_next(a)
            self.assertEqual(b.endian, endian)
            # nothing to permute
            self.assertEqual(b, a)

    def test_next_explicit(self):
        for perm, endian in [
                (['100', '010', '001', '100'], 'little'),
                (['00001', '00010', '00100', '01000', '10000'], 'big'),
                (['0011', '0101', '0110', '1001', '1010',
                  '1100', '0011'], 'big'),
                (['0000111', '1110000'], 'little'),
        ]:
            a = bitarray(perm[0], endian)
            for s in perm[1:]:
                a = lexico_next(a)
                self.assertEqual(a, bitarray(s))

    def check_cycle(self, n, k):
        endian = choice(["little", "big"])
        a0 = bitarray(n, endian)
        a0[:k] = 1
        if endian == "big":
            a0.reverse()

        ncycle = math.comb(n, k)  # cycle length
        self.assertTrue(ncycle >= 2)
        coll = set()
        a = a0.copy()
        for i in range(ncycle):
            coll.add(frozenbitarray(a))
            b = lexico_next(a)
            self.assertEqual(len(b), n)
            self.assertEqual(b.count(), k)
            self.assertEqual(b.endian, endian)
            self.assertNotEqual(a, b)
            if b == a0:
                self.assertEqual(i, ncycle - 1)
                self.assertTrue(ba2int(b) < ba2int(a))
                break
            self.assertTrue(ba2int(b) > ba2int(a))
            a = b
        else:
            self.fail()

        self.assertEqual(len(coll), ncycle)

    def test_cycle(self):
        for _ in range(20):
            n = randrange(2, 10)
            k = randrange(1, n)
            self.check_cycle(n, k)

    def test_next_random(self):
        for _ in range(100):
            endian = choice(["little", "big"])
            n = randrange(2, 1_000)
            k = randrange(1, n)
            a = random_k(n, k, endian)
            b = lexico_next(a)
            self.assertEqual(len(b), n)
            self.assertEqual(b.count(), k)
            self.assertEqual(b.endian, endian)
            self.assertNotEqual(a, b)
            if ba2int(a) > ba2int(b):
                c = a.copy()
                c.sort(endian == 'big')
                self.assertEqual(a, c)
                self.assertEqual(b, a[::-1])

    # -------------------  lexico_all  -------------------

    def test_all_explicit(self):
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
            lst = list(lexico_all(n, k, 'big'))
            self.assertEqual(len(lst), math.comb(n, k))
            self.assertEqual(lst, [bitarray(s) for s in res])
            if n == 0:
                continue
            a = lst[0]
            for i in range(20):
                self.assertEqual(a, bitarray(res[i % len(lst)]))
                a = lexico_next(a)

    def test_all_perm(self):
        n, k = 17, 5
        endian=choice(["little", "big"])

        prev = None
        cnt = 0
        coll = set()
        for a in lexico_all(n, k, endian):
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
                self.assertNotEqual(a, first)
                self.assertEqual(lexico_next(prev), a)
                self.assertTrue(ba2int(prev) < ba2int(a))
            prev = a
            cnt += 1

        self.assertEqual(cnt, math.comb(n, k))
        self.assertEqual(len(coll), cnt)

        # a is now the last permutation
        last = a.copy()
        self.assertTrue(ba2int(first) < ba2int(last))
        self.assertEqual(last, first[::-1])

    def test_all_order(self):
        n, k = 10, 5
        for a, b in pairwise(lexico_all(n, k, 'little')):
            self.assertTrue(ba2int(b) > ba2int(a))
            self.assertEqual(lexico_next(a), b)


if __name__ == '__main__':
    unittest.main()
