from random import randint
import unittest

from bitarray import bitarray
from bitarray.util import urandom
from bitarray.test_bitarray import Util


class InternalTests(unittest.TestCase, Util):

    def test_shift_r8_empty(self):
        a = bitarray()
        a._shift_r8(0, 0, 3)
        self.assertEqual(a, bitarray())

        a = urandom(80)
        b = a.copy()
        a._shift_r8(7, 7, 5)
        self.assertEqual(a, b)

    def test_shift_r8_explicit(self):
        a = bitarray('11000100 01111000 10110101 11101011 11001000')
        b = bitarray(a)
        a._shift_r8(2, 2, 7)
        self.assertEqual(a, b)
        a._shift_r8(1, 4, 5)
        self.assertEqual(
            a, bitarray('11000100 00000011 11000101 10101111 11001000'))

    def shift_r8(self, x, a, b, n):
        self.assertTrue(a <= b)
        self.assertTrue(n < 8)
        y = x.tolist()
        if n > 0 and a != b:
            y[8 * a : 8 * b] = n * [0] + y[8 * a : 8 * b - n]
        self.assertEqual(len(y), len(x))
        return bitarray(y, x.endian())

    def test_shift_r8_random(self):
        for N in range(1, 100):
            x = urandom(8 * N, self.random_endian())
            cx = x.copy()
            a = randint(0, N)
            b = randint(0, N)
            n = randint(0, 7)
            if a <= b:
                x._shift_r8(a, b, n)
                self.assertEQUAL(x, self.shift_r8(cx, a, b, n))
            else:
                self.assertRaises(ValueError, x._shift_r8, a, b, n)


if __name__ == '__main__':
    unittest.main()
