import sys
import unittest
from random import randrange

from bitarray import bitarray, get_default_endian
from bitarray.util import zeros, urandom, intervals
from bitarray._util import leb128_encode, leb128_decode, rl_encode, rl_decode
from bitarray.test_util import OPT_ENDIANS


class LEB128_Tests(unittest.TestCase):

    def test_explicit(self):
        for i, b in [
                (      0, b'\x00'),
                (    127, b'\x7f'),
                (    128, b'\x80\x01'),
                ( 16_383, b'\xff\x7f'),
                ( 16_384, b'\x80\x80\x01'),
                (624_485, b'\xe5\x8e\x26'),  # wikipedia LEB128
        ]:
            self.assertEqual(leb128_encode(i), b)
            self.assertEqual(leb128_decode(b), i)
            it = iter(b)
            self.assertEqual(leb128_decode(it), i)
            self.assertRaises(StopIteration, next, it)
            it = iter(b + b'XYZ')
            self.assertEqual(leb128_decode(it), i)
            self.assertEqual(next(it), ord(b'X'))

    def test_encode_errors(self):
        E = leb128_encode
        self.assertRaises(ValueError, E, -1)
        self.assertRaises(ValueError, E, -123)
        self.assertRaises(OverflowError, E, sys.maxsize + 1)
        self.assertRaises(OverflowError, E, -sys.maxsize - 2)
        self.assertRaises(TypeError, E, 1.0)

    def test_decode_ambiguity(self):
        # ULEB128 permits overlong representations.
        for b in b'\x00', b'\x80\x00':
            self.assertEqual(leb128_decode(b), 0)
        for b in b'\x0a', b'\x8a\x00', b'\x8a\x80\x00':
            self.assertEqual(leb128_decode(b), 10)

    def test_decode_types(self):
        lst = [0xe5, 0x8e, 0x26]
        for b in lst, bytes(lst), bytearray(lst), iter(lst):
            self.assertEqual(leb128_decode(b), 624_485)

    def test_decode_errors(self):
        D = leb128_decode
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
        self.assertEqual(leb128_encode(i), b)
        self.assertEqual(leb128_decode(b), i)

    def test_range(self):
        for i in range(1000):
            b = leb128_encode(i)
            self.assertEqual(leb128_decode(b), i)

    def test_random(self):
        for _ in range(10000):
            i = randrange(1_000_000_000)
            b = leb128_encode(i)
            self.assertEqual(leb128_decode(b), i)


class RL_Tests(unittest.TestCase):

    def test_explicit(self):
        for s, b in [
                ("",    b'0\x00'),
                ("0",   b'0\x01\x00'),
                ("1",   b'1\x01\x01'),
                ("10",  b'1\x02\x01\x00'),
                ("01",  b'0\x02\x01\x01'),
                ("1" + 63 * "0", b'1\x40\x01\x00'),
                ("0" + 63 * "1", b'0\x40\x01\x3f'),
        ]:
            a = bitarray(s)
            self.assertEqual(rl_encode(a), b)
            self.assertEqual(rl_decode(b), a)
            it = iter(b)
            self.assertEqual(rl_decode(it), a)
            self.assertRaises(StopIteration, next, it)
            it = iter(b + b'XYZ')
            self.assertEqual(rl_decode(it), a)
            self.assertEqual(next(it), ord(b'X'))

    def test_decode_errors(self):
        # incomplete stream
        for b in b'', b'0', b'0\x01', :
            self.assertRaises(StopIteration, rl_decode, b)

        # invalid first bit
        for b in b'\x00', b'\x01', b'2':
            self.assertRaises(ValueError, rl_decode, b)

        # sequence of 1s at [0x01 : 0x41] exceeds nbits
        b = b'0\x40\x01\x40'
        self.assertRaises(ValueError, rl_decode, b)

    def test_decode_ambiguity(self):
        # For an empty bitarray either first-bit byte works.
        for b in b'0\x00', b'1\x00':
            self.assertEqual(rl_decode(b), bitarray())

        # For an all-zero bitarray, either first-bit byte works when
        # immediately followed by the zero terminator.
        for b in b'0\x01\x00', b'1\x01\x00':
            self.assertEqual(rl_decode(b), bitarray('0'))

        # Trailing zeros may be represented by the zero terminator or
        # by an explicit final zero-run length.
        for b in b'1\x02\x01\x00', b'1\x02\x01\x01':
            self.assertEqual(rl_decode(b), bitarray('10'))

        # Partial trailing-zero run followed by the zero terminator.
        for b in [
                b'1\x03\x01\x00',      # canonical
                b'1\x03\x01\x02',      # complete trailing-zero run
                b'1\x03\x01\x01\x00',  # partial zero run, then terminator
        ]:
            self.assertEqual(rl_decode(b), bitarray('100'))

        # ULEB128 permits overlong representations.
        for b in b'0\x00', b'0\x80\x00':
            self.assertEqual(rl_decode(b), bitarray())

    def test_decode_endian(self):
        b = b'0\x02\x01\x01'
        for endian in OPT_ENDIANS:
            a = rl_decode(b, endian)
            self.assertEqual(a.endian, endian or get_default_endian())
            self.assertEqual(a, bitarray("01"))

    def test_intervals(self):
        a = bitarray("00000011111111111111110000000111000000")
        b = bytearray([ord("0")])
        b.extend(leb128_encode(len(a)))
        for unused_value, start, stop in intervals(a):
            b.extend(leb128_encode(stop - start))
        self.assertEqual(rl_decode(b), a)
        # Because `a` has trailing zeros, this is not the canonical code
        self.assertNotEqual(b, rl_encode(a))

    def test_alternate(self):
        a = zeros(1024)
        a[:512] = 1
        a *= 8
        b = rl_encode(a)
        self.assertEqual(rl_decode(b), a)

    def test_ones_zeros(self):
        for n in range(1000):
            a = bitarray(n)
            for v in 0, 1:
                a.setall(v)
                b = rl_encode(a)
                self.assertEqual(rl_decode(b), a)

    def test_random(self):
        for _ in range(10):
            n = randrange(1000)
            a = urandom(n)
            b = rl_encode(a)
            self.assertEqual(rl_decode(b), a)


# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
