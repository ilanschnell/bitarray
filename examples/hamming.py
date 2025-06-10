# https://www.youtube.com/watch?v=b3NxrZOu_CE
# https://en.wikipedia.org/wiki/Hamming_code
from bitarray.util import xor_indices, int2ba, parity


class Hamming:

    def __init__(self, r):
        self.r = r
        self.n = 1 << r          # block length
        self.k = self.n - r - 1  # message length

        self.parity_bits = [0]   # the 0th bit is to make overall parity 0
        i = 1
        while i < self.n:
            self.parity_bits.append(i)
            i <<= 1

    def send(self, a):
        "encode message inplace"
        if len(a) != self.k:
            raise ValueError("expected bitarray of message length %d" % self.k)
        for i in self.parity_bits:
            a.insert(i, 0)

        # prepare block
        c = xor_indices(a)
        a[self.parity_bits[1:]] = int2ba(c, length=self.r, endian="little")
        a[0] = parity(a)

    def receive(self, a):
        "decode inplace and return number of bit errors"
        if len(a) != self.n:
            raise ValueError("expected bitarray of block length %d" % self.n)
        p = parity(a)
        c = xor_indices(a)
        a.invert(c)  # fix bit error
        del a[self.parity_bits]

        if p:  # overall parity is wrong, so we have a 1 bit error
            return 1
        if c:  # overall parity is OK, but since we have wrong partial
               # parities, there must have been 2 bit errors
            return 2
        # overall parity as well as partial parities as fine, so no error
        return 0

# ---------------------------------------------------------------------------

from random import getrandbits, randint
import unittest

from bitarray import bitarray
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

    def check_well_prepared(self, a):
        n = len(a)
        self.assertEqual(n & (n - 1), 0)     # n is power of 2
        self.assertEqual(xor_indices(a), 0)  # partial parity bits are 0
        self.assertEqual(parity(a), 0)       # overall parity is 0

    def test_example(self):
        a = bitarray("   0  010  111 0110")
        #             012  4    8
        c = a.copy()
        b = bitarray("1100 1010 1111 0110")
        #             012  4    8
        h = Hamming(4)
        self.check_well_prepared(b)
        h.send(a)
        self.assertEqual(a, b)
        a.invert(10)
        self.assertEqual(h.receive(a), 1)
        self.assertEqual(a, c)

    def test_send(self):
        for _ in range(1000):
            h = Hamming(randint(2, 10))
            a = urandom(h.k)
            h.send(a)
            self.assertEqual(len(a), h.n)
            self.check_well_prepared(a)

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

            res = h.receive(a)
            self.assertEqual(len(a), h.k)
            if dist <= 1:
                self.check_well_prepared(t)
                self.assertEqual(a, b)
            self.assertEqual(res, dist)

if __name__ == '__main__':
    unittest.main()
