# https://www.youtube.com/watch?v=b3NxrZOu_CE
# https://en.wikipedia.org/wiki/Hamming_code
from bitarray.util import xor_indices, int2ba, parity


class Hamming:

    def __init__(self, r):
        self.r = r
        self.n = 1 << r          # block length
        self.k = self.n - r - 1  # message length

        self.parity_bits = [0]  # the 0th bit is to make overall parity 0
        i = 1
        while i < self.n:
            self.parity_bits.append(i)
            i <<= 1

    def send(self, a):
        if len(a) != self.k:
            raise ValueError("expected bitarray of message length %d" % self.k)
        for i in self.parity_bits:
            a.insert(i, 0)

        c = xor_indices(a)
        a[self.parity_bits[1:]] = int2ba(c, length=self.r, endian="little")
        a[0] = parity(a)

    def is_well_prepared(self, a):
        if len(a) != self.n:
            raise ValueError("expected bitarray of block length %d" % self.n)
        return xor_indices(a) == 0 and parity(a) == 0

    def receive(self, a):
        "decode inplace and return number of bit errors"
        if len(a) != self.n:
            raise ValueError("expected bitarray of block length %d" % self.n)
        p = parity(a)
        c = xor_indices(a)
        a.invert(c)
        del a[self.parity_bits]

        if p:  # overall parity is wrong, so we have a 1 bit error
            return 1
        if c:  # overall parity is ok, but since we have wrong partial
               # parities, there must have been 2 bit errors
            return 2
        # overall parity as well as partial parities as fine, so no error
        return 0

# ---------------------------------------------------------------------------

from random import getrandbits, randint
import unittest

from bitarray.util import urandom, count_xor
from bitarray.test_bitarray import Util


class HammingTests(unittest.TestCase, Util):

    def test_init(self):
        for r, n, k in [
                (1,  2,  0),    ( 8,   256,   247),
                (2,  4,  1),    (16, 65536, 65519),
                (3,  8,  4),
                (4, 16, 11),
                (5, 32, 26),
                (6, 64, 57),
        ]:
            h = Hamming(r)
            self.assertEqual(h.r, r)
            self.assertEqual(h.n, n)
            self.assertEqual(h.k, k)
            self.assertEqual(len(h.parity_bits), h.r + 1)

    def test_send(self):
        for _ in range(1000):
            h = Hamming(randint(2, 10))
            a = urandom(h.k)
            h.send(a)
            self.assertEqual(len(a), h.n)
            self.assertTrue(h.is_well_prepared(a))

    def test_receive(self):
        for _ in range(1000):
            h = Hamming(randint(2, 10))
            a = urandom(h.k)
            b = a.copy()
            h.send(a)
            t = a.copy()
            for _ in range(randint(0, 2)):
                a.invert(getrandbits(h.r))
            dist = count_xor(a, t)
            self.assertTrue(0 <= dist <= 2)
            is_well = h.is_well_prepared(a)

            res = h.receive(a)
            self.assertEqual(len(a), h.k)
            if dist <= 1:
                self.assertEqual(is_well, dist == 0)
                self.assertEqual(a, b)
            self.assertEqual(res, dist)

if __name__ == '__main__':
    unittest.main()
