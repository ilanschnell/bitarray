import sys
import unittest
from random import randrange

from bitarray._util import (uleb128_encode, uleb128_decode,
                            rl_encode, rl_decode)
from bitarray.test_util import RL_Util


class ULEB128_Tests(unittest.TestCase):

    def test_explicit(self):
        for i, b in [
                (      0, b'\x00'),
                (    127, b'\x7f'),
                (    128, b'\x80\x01'),
                ( 16_383, b'\xff\x7f'),
                ( 16_384, b'\x80\x80\x01'),
                (624_485, b'\xe5\x8e\x26'),  # wikipedia LEB128
        ]:
            self.assertEqual(uleb128_encode(i), b)
            self.assertEqual(uleb128_decode(b), i)
            it = iter(b)
            self.assertEqual(uleb128_decode(it), i)
            self.assertRaises(StopIteration, next, it)
            it = iter(b + b'XYZ')
            self.assertEqual(uleb128_decode(it), i)
            self.assertEqual(next(it), ord(b'X'))

    def test_encode_errors(self):
        E = uleb128_encode
        self.assertRaises(ValueError, E, -1)
        self.assertRaises(ValueError, E, -123)
        self.assertRaises(OverflowError, E, sys.maxsize + 1)
        self.assertRaises(OverflowError, E, -sys.maxsize - 2)
        self.assertRaises(TypeError, E, 1.0)

    def test_decode_ambiguity(self):
        # ULEB128 permits overlong representations.
        for b in b'\x00', b'\x80\x00':
            self.assertEqual(uleb128_decode(b), 0)
        for b in b'\x0a', b'\x8a\x00', b'\x8a\x80\x00':
            self.assertEqual(uleb128_decode(b), 10)

    def test_decode_types(self):
        lst = [0xe5, 0x8e, 0x26]
        for b in lst, bytes(lst), bytearray(lst), iter(lst):
            self.assertEqual(uleb128_decode(b), 624_485)

    def test_decode_errors(self):
        D = uleb128_decode
        self.assertRaises(ValueError, D, [-1])
        self.assertRaises(ValueError, D, [0xff, 256])
        self.assertRaises(TypeError, D, "String")
        for b in b'', b'\xff', b'\xff\xff':
            self.assertRaises(StopIteration, D, b)
        self.assertRaises(OverflowError, D, 9 * b'\xff')

    def test_maxsize(self):
        i = sys.maxsize
        width = i.bit_length() + 1
        if width == 64:
            b = 8 * b'\xff' + b'\x7f'
        elif width == 32:
            b = 4 * b'\xff' + b'\x07'
        self.assertEqual(uleb128_encode(i), b)
        self.assertEqual(uleb128_decode(b), i)

    def test_range(self):
        for i in range(1000):
            b = uleb128_encode(i)
            self.assertEqual(uleb128_decode(b), i)

    def test_random(self):
        for _ in range(10000):
            i = randrange(1_000_000_000)
            b = uleb128_encode(i)
            self.assertEqual(uleb128_decode(b), i)


class RL_Tests(unittest.TestCase, RL_Util):

    def test_random_runs(self):
        # only tests RL_Util itself
        a = self.random_runs(0, 0)
        self.assertEqual(len(a), 0)

        for n in range(1, 20):
            for k in range(1, n + 1):
                a = self.random_runs(n, k)
                self.assertEqual(len(a), n)
                self.assertEqual(self.runs(a), k)
                self.assertEqual(a[0] ^ a[-1], not k % 2)

    def test_output_resize(self):
        # tests resizing output buffer
        n = 10_000_000
        k = randrange(500_000, 1000_000)
        a = self.random_runs(n, k)
        b = rl_encode(a)
        self.assertEqual(rl_decode(b), a)
        self.assertEqual(len(a), n)
        self.assertGreater(len(b), 32768)


# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
