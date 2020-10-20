"""
Tests for bitarray.util module
"""
from __future__ import absolute_import

import os
import sys
import unittest
from string import hexdigits
from random import choice, randint, random
try:
    from collections import Counter
except ImportError:
    pass

from bitarray import (bitarray, frozenbitarray, bits2bytes, decodetree,
                      get_default_endian, _set_default_endian)
from bitarray.test_bitarray import Util

from bitarray.util import (zeros, make_endian, rindex, strip, count_n,
                           count_and, count_or, count_xor, subset,
                           ba2hex, hex2ba, ba2int, int2ba, huffman_code)

if sys.version_info[0] == 3:
    unicode = str

tests = []

# ---------------------------------------------------------------------------

class TestsZeros(unittest.TestCase):

    def test_1(self):
        for default_endian in 'big', 'little':
            _set_default_endian(default_endian)
            a = zeros(0)
            self.assertEqual(a, bitarray())
            self.assertEqual(a.endian(), default_endian)

            b  = zeros(0, endian=None)
            self.assertEqual(b.endian(), default_endian)

            for n in range(100):
                a = zeros(n)
                self.assertEqual(a, bitarray(n * '0'))

            for endian in 'big', 'little':
                a = zeros(3, endian)
                self.assertEqual(a, bitarray('000'))
                self.assertEqual(a.endian(), endian)

    def test_wrong_args(self):
        self.assertRaises(TypeError, zeros) # no argument
        self.assertRaises(TypeError, zeros, '')
        self.assertRaises(TypeError, zeros, bitarray())
        self.assertRaises(TypeError, zeros, [])
        self.assertRaises(TypeError, zeros, 1.0)
        self.assertRaises(ValueError, zeros, -1)

        self.assertRaises(TypeError, zeros, 0, 1) # endian not string
        self.assertRaises(ValueError, zeros, 0, 'foo') # endian wrong string

tests.append(TestsZeros)

# ---------------------------------------------------------------------------

class TestsMakeEndian(unittest.TestCase, Util):

    def test_simple(self):
        a = bitarray('1110001', endian='big')
        b = make_endian(a, 'big')
        self.assertTrue(b is a)
        c = make_endian(a, 'little')
        self.assertTrue(c == a)
        self.assertEqual(c.endian(), 'little')

        # wrong arguments
        self.assertRaises(TypeError, make_endian, '', 'big')
        self.assertRaises(TypeError, make_endian, bitarray(), 1)
        self.assertRaises(ValueError, make_endian, bitarray(), 'foo')

    def test_empty(self):
        a = bitarray(endian='little')
        b = make_endian(a, 'big')
        self.assertTrue(b == a)
        self.assertEqual(len(b), 0)
        self.assertEqual(b.endian(), 'big')

    def test_from_frozen(self):
        a = frozenbitarray('1101111', 'big')
        b = make_endian(a, 'big')
        self.assertTrue(b is a)
        c = make_endian(a, 'little')
        self.assertTrue(c == a)
        self.assertEqual(c.endian(), 'little')

    def test_random(self):
        for a in self.randombitarrays():
            aa = a.copy()
            for endian in 'big', 'little':
                b = make_endian(a, endian)
                self.assertEqual(a, b)
                self.assertEqual(b.endian(), endian)
                if a.endian() == endian:
                    self.assertTrue(b is a)
            self.assertEQUAL(a, aa)

tests.append(TestsMakeEndian)

# ---------------------------------------------------------------------------

class TestsRindex(unittest.TestCase, Util):

    def test_simple(self):
        self.assertRaises(TypeError, rindex)
        self.assertRaises(TypeError, rindex, None)
        self.assertRaises(TypeError, rindex, bitarray(), 1, 2)
        for endian in 'big', 'little':
            a = bitarray('00010110000', endian)
            self.assertEqual(rindex(a), 6)
            self.assertEqual(rindex(a, 1), 6)
            self.assertEqual(rindex(a, 'A'), 6)
            self.assertEqual(rindex(a, True), 6)

            a = bitarray('00010110111', endian)
            self.assertEqual(rindex(a, 0), 7)
            self.assertEqual(rindex(a, None), 7)
            self.assertEqual(rindex(a, False), 7)

            a = frozenbitarray('00010110111', endian)
            self.assertEqual(rindex(a, 0), 7)
            self.assertEqual(rindex(a, None), 7)
            self.assertEqual(rindex(a, False), 7)

            for v in 0, 1:
                self.assertRaises(ValueError, rindex,
                                  bitarray(0, endian), v)
            self.assertRaises(ValueError, rindex,
                              bitarray('000', endian), 1)
            self.assertRaises(ValueError, rindex,
                              bitarray('11111', endian), 0)

    def test_random(self):
        for a in self.randombitarrays():
            v = randint(0, 1)
            try:
                i = rindex(a, v)
            except ValueError:
                i = None
            s = a.to01()
            try:
                j = s.rindex(str(v))
            except ValueError:
                j = None
            self.assertEqual(i, j)

    def test_3(self):
        for _ in range(100):
            n = randint(1, 100000)
            v = randint(0, 1)
            a = bitarray(n)
            a.setall(1 - v)
            lst = [randint(0, n - 1) for _ in range(100)]
            for i in lst:
                a[i] = v
            self.assertEqual(rindex(a, v), max(lst))

    def test_one_set(self):
        for _ in range(100):
            N = randint(1, 10000)
            a = bitarray(N)
            a.setall(0)
            a[randint(0, N - 1)] = 1
            self.assertEqual(rindex(a), a.index(1))

tests.append(TestsRindex)

# ---------------------------------------------------------------------------

class TestsStrip(unittest.TestCase, Util):

    def test_simple(self):
        self.assertRaises(TypeError, strip, '0110')
        self.assertRaises(TypeError, strip, bitarray(), 123)
        self.assertRaises(ValueError, strip, bitarray(), 'up')
        for default_endian in 'big', 'little':
            _set_default_endian(default_endian)
            a = bitarray('00010110000')
            self.assertEQUAL(strip(a), bitarray('0001011'))
            self.assertEQUAL(strip(a, 'left'), bitarray('10110000'))
            self.assertEQUAL(strip(a, 'both'), bitarray('1011'))
            b = frozenbitarray('00010110000')
            self.assertEqual(strip(b, 'both'), bitarray('1011'))

        for mode in 'left', 'right', 'both':
            self.assertEqual(strip(bitarray('000'), mode), bitarray())
            self.assertEqual(strip(bitarray(), mode), bitarray())

    def test_random(self):
        for a in self.randombitarrays():
            b = a.copy()
            s = a.to01()
            self.assertEqual(strip(a, 'left'), bitarray(s.lstrip('0')))
            self.assertEqual(strip(a, 'right'), bitarray(s.rstrip('0')))
            self.assertEqual(strip(a, 'both'), bitarray(s.strip('0')))
            self.assertEQUAL(a, b)

    def test_one_set(self):
        for _ in range(100):
            N = randint(1, 10000)
            a = bitarray(N)
            a.setall(0)
            a[randint(0, N - 1)] = 1
            self.assertEqual(strip(a, 'both'), bitarray('1'))

tests.append(TestsStrip)

# ---------------------------------------------------------------------------

class TestsCount_N(unittest.TestCase, Util):

    @staticmethod
    def count_n(a, n):
        "return the index i for which a[:i].count() == n"
        i, j = n, a.count(1, 0, n)
        while j < n:
            if a[i]:
                j += 1
            i += 1
        return i

    def check_result(self, a, n, i):
        self.assertEqual(a.count(1, 0, i), n)
        if i:
            self.assertTrue(a[i - 1])

    def test_simple(self):
        a = bitarray('111110111110111110111110011110111110111110111000')
        b = a.copy()
        self.assertEqual(len(a), 48)
        self.assertEqual(a.count(), 37)
        self.assertRaises(TypeError, count_n, '', 0)
        self.assertEqual(count_n(a, 0), 0)
        self.assertEqual(count_n(a, 20), 23)
        self.assertEqual(count_n(a, 37), 45)
        self.assertRaisesMessage(ValueError, "non-negative integer expected",
                                 count_n, a, -1) # n < 0
        self.assertRaisesMessage(ValueError, "n larger than bitarray size",
                                 count_n, a, 49) # n > len(a)
        self.assertRaisesMessage(ValueError, "n exceeds total count",
                                 count_n, a, 38) # n > a.count()
        self.assertRaises(TypeError, count_n, a, "7")
        for n in range(0, 37):
            i = count_n(a, n)
            self.check_result(a, n, i)
            self.assertEqual(a[:i].count(), n)
            self.assertEqual(i, self.count_n(a, n))
        self.assertEQUAL(a, b)

    def test_frozen(self):
        a = frozenbitarray('001111101111101111101111100111100')
        self.assertEqual(len(a), 33)
        self.assertEqual(a.count(), 24)
        self.assertRaises(TypeError, count_n, '', 0)
        self.assertEqual(count_n(a, 0), 0)
        self.assertEqual(count_n(a, 10), 13)
        self.assertEqual(count_n(a, 24), 31)
        self.assertRaises(ValueError, count_n, a, -1) # n < 0
        self.assertRaises(ValueError, count_n, a, 25) # n > a.count()
        self.assertRaises(ValueError, count_n, a, 34) # n > len(a)
        self.assertRaises(TypeError, count_n, a, "7")

    def test_large(self):
        for N in list(range(100)) + [1000, 10000, 100000]:
            a = bitarray(N)
            v = randint(0, 1)
            a.setall(v - 1)
            for _ in range(randint(0, min(N, 100))):
                a[randint(0, N - 1)] = v
            n = randint(0, a.count())
            self.check_result(a, n, count_n(a, n))
            # check for total count
            tc = a.count()
            self.assertTrue(count_n(a, tc) <= N)
            self.assertRaises(ValueError, count_n, a, tc + 1)

    def test_one_set(self):
        N = 100000
        for _ in range(10):
            a = bitarray(N)
            a.setall(0)
            self.assertEqual(count_n(a, 0), 0)
            self.assertRaises(ValueError, count_n, a, 1)
            i = randint(0, N - 1)
            a[i] = 1
            self.assertEqual(count_n(a, 1), i + 1)
            self.assertRaises(ValueError, count_n, a, 2)

    def test_random(self):
        for a in self.randombitarrays():
            n = a.count() // 2
            i = count_n(a, n)
            self.check_result(a, n, i)

tests.append(TestsCount_N)

# ---------------------------------------------------------------------------

class TestsBitwiseCount(unittest.TestCase, Util):

    def test_count_byte(self):
        ones = bitarray(8)
        ones.setall(1)
        zeros = bitarray(8)
        zeros.setall(0)
        for i in range(0, 256):
            a = bitarray()
            a.frombytes(bytes(bytearray([i])))
            cnt = a.count()
            self.assertEqual(count_and(a, zeros), 0)
            self.assertEqual(count_and(a, ones), cnt)
            self.assertEqual(count_and(a, a), cnt)
            self.assertEqual(count_or(a, zeros), cnt)
            self.assertEqual(count_or(a, ones), 8)
            self.assertEqual(count_or(a, a), cnt)
            self.assertEqual(count_xor(a, zeros), cnt)
            self.assertEqual(count_xor(a, ones), 8 - cnt)
            self.assertEqual(count_xor(a, a), 0)

    def test_bit_count1(self):
        a = bitarray('001111')
        aa = a.copy()
        b = bitarray('010011')
        bb = b.copy()
        self.assertEqual(count_and(a, b), 2)
        self.assertEqual(count_or(a, b), 5)
        self.assertEqual(count_xor(a, b), 3)
        for f in count_and, count_or, count_xor:
            # not two arguments
            self.assertRaises(TypeError, f)
            self.assertRaises(TypeError, f, a)
            self.assertRaises(TypeError, f, a, b, 3)
            # wrong argument types
            self.assertRaises(TypeError, f, a, '')
            self.assertRaises(TypeError, f, '1', b)
            self.assertRaises(TypeError, f, a, 4)
        self.assertEQUAL(a, aa)
        self.assertEQUAL(b, bb)

        b.append(1)
        for f in count_and, count_or, count_xor:
            self.assertRaises(ValueError, f, a, b)
            self.assertRaises(ValueError, f,
                              bitarray('110', 'big'),
                              bitarray('101', 'little'))

    def test_bit_count_frozen(self):
        a = frozenbitarray('001111')
        b = frozenbitarray('010011')
        self.assertEqual(count_and(a, b), 2)
        self.assertEqual(count_or(a, b), 5)
        self.assertEqual(count_xor(a, b), 3)

    def test_bit_count2(self):
        for n in list(range(50)) + [randint(1000, 2000)]:
            a = bitarray()
            a.frombytes(os.urandom(bits2bytes(n)))
            del a[n:]
            b = bitarray()
            b.frombytes(os.urandom(bits2bytes(n)))
            del b[n:]
            self.assertEqual(count_and(a, b), (a & b).count())
            self.assertEqual(count_or(a, b),  (a | b).count())
            self.assertEqual(count_xor(a, b), (a ^ b).count())

tests.append(TestsBitwiseCount)

# ---------------------------------------------------------------------------

class TestsSubset(unittest.TestCase, Util):

    def test_basic(self):
        a = frozenbitarray('0101')
        b = bitarray('0111')
        self.assertTrue(subset(a, b))
        self.assertFalse(subset(b, a))
        self.assertRaises(TypeError, subset)
        self.assertRaises(TypeError, subset, a, '')
        self.assertRaises(TypeError, subset, '1', b)
        self.assertRaises(TypeError, subset, a, 4)
        b.append(1)
        self.assertRaises(ValueError, subset, a, b)

    def subset_simple(self, a, b):
        return (a & b).count() == a.count()

    def test_True(self):
        for a, b in [('', ''), ('0', '1'), ('0', '0'), ('1', '1'),
                     ('000', '111'), ('0101', '0111'),
                     ('000010111', '010011111')]:
            a, b = bitarray(a), bitarray(b)
            self.assertTrue(subset(a, b) is True)
            self.assertTrue(self.subset_simple(a, b) is True)

    def test_False(self):
        for a, b in [('1', '0'), ('1101', '0111'),
                     ('0000101111', '0100111011')]:
            a, b = bitarray(a), bitarray(b)
            self.assertTrue(subset(a, b) is False)
            self.assertTrue(self.subset_simple(a, b) is False)

    def test_random(self):
        for a in self.randombitarrays(start=1):
            b = a.copy()
            # we set one random bit in b to 1, so a is always a subset of b
            b[randint(0, len(a) - 1)] = 1
            self.assertTrue(subset(a, b))
            # but b in not always a subset of a
            self.assertEqual(subset(b, a), self.subset_simple(b, a))
            # we set all bits in a, which ensures that b is a subset of a
            a.setall(1)
            self.assertTrue(subset(b, a))

tests.append(TestsSubset)

# ---------------------------------------------------------------------------

CODEDICT = {'little': {}, 'big': {
    '0': bitarray('0000'),    '1': bitarray('0001'),
    '2': bitarray('0010'),    '3': bitarray('0011'),
    '4': bitarray('0100'),    '5': bitarray('0101'),
    '6': bitarray('0110'),    '7': bitarray('0111'),
    '8': bitarray('1000'),    '9': bitarray('1001'),
    'a': bitarray('1010'),    'b': bitarray('1011'),
    'c': bitarray('1100'),    'd': bitarray('1101'),
    'e': bitarray('1110'),    'f': bitarray('1111'),
}}
for k, v in CODEDICT['big'].items():
    CODEDICT['little'][k] = v[::-1]


class TestsHexlify(unittest.TestCase, Util):

    def test_swap_hilo_bytes(self):
        from bitarray._util import _swap_hilo_bytes

        self.assertEqual(len(_swap_hilo_bytes), 256)
        for i in range(256):
            byte = bytes(bytearray([i]))
            a = bitarray()
            a.frombytes(byte)
            self.assertEqual(len(a), 8)

            b = a[4:8] + a[0:4]
            self.assertEqual(b.tobytes(),
                             byte.translate(_swap_hilo_bytes))
            # with just _swap_hilo_bytes[i] we'd get an integer on Py3
            self.assertEqual(b.tobytes(), _swap_hilo_bytes[i:i + 1])

    def test_ba2hex(self):
        self.assertEqual(ba2hex(bitarray(0, 'big')), '')
        self.assertEqual(ba2hex(bitarray('1110', 'big')), 'e')
        self.assertEqual(ba2hex(bitarray('1110', 'little')), '7')
        self.assertEqual(ba2hex(bitarray('00000001', 'big')), '01')
        self.assertEqual(ba2hex(bitarray('10000000', 'big')), '80')
        self.assertEqual(ba2hex(bitarray('00000001', 'little')), '08')
        self.assertEqual(ba2hex(bitarray('10000000', 'little')), '10')
        self.assertEqual(ba2hex(frozenbitarray('11000111', 'big')), 'c7')
        # length not multiple of 4
        self.assertRaises(ValueError, ba2hex, bitarray('10'))
        self.assertRaises(TypeError, ba2hex, '101')

        c = ba2hex(bitarray('1101', 'big'))
        self.assertIsInstance(c, str)

        for n in range(7):
            a = bitarray(n * '1111', 'big')
            b = a.copy()
            self.assertEqual(ba2hex(a), n * 'f')
            # ensure original object wasn't altered
            self.assertEQUAL(a, b)

    def test_hex2ba(self):
        _set_default_endian('big')
        self.assertEqual(hex2ba(''), bitarray())
        for c in 'e', 'E', b'e', b'E', unicode('e'), unicode('E'):
            a = hex2ba(c)
            self.assertEqual(a.to01(), '1110')
            self.assertEqual(a.endian(), 'big')
        self.assertEQUAL(hex2ba('01'), bitarray('00000001', 'big'))
        self.assertEQUAL(hex2ba('08', 'little'),
                         bitarray('00000001', 'little'))
        self.assertRaises(Exception, hex2ba, '01a7x89')
        self.assertRaises(TypeError, hex2ba, 0)

    @staticmethod
    def hex2ba(s, endian=None):
        a = bitarray(0, endian or get_default_endian())
        a.encode(CODEDICT[a.endian()], s)
        return a

    @staticmethod
    def ba2hex(a):
        return ''.join(a.iterdecode(CODEDICT[a.endian()]))

    def test_explicit(self):
        data = [ #     little  big                  little  big
            ('',       '',     ''),
            ('0000',   '0',    '0'),     ('0001',   '8',    '1'),
            ('1000',   '1',    '8'),     ('1001',   '9',    '9'),
            ('0100',   '2',    '4'),     ('0101',   'a',    '5'),
            ('1100',   '3',    'c'),     ('1101',   'b',    'd'),
            ('0010',   '4',    '2'),     ('0011',   'c',    '3'),
            ('1010',   '5',    'a'),     ('1011',   'd',    'b'),
            ('0110',   '6',    '6'),     ('0111',   'e',    '7'),
            ('1110',   '7',    'e'),     ('1111',   'f',    'f'),
            ('10001100',             '13',    '8c'),
            ('100011001110',         '137',   '8ce'),
            ('1000110011101111',     '137f',  '8cef'),
            ('10001100111011110100', '137f2', '8cef4'),
        ]
        for bs, hex_le, hex_be in data:
            a_be = bitarray(bs, 'big')
            a_le = bitarray(bs, 'little')
            self.assertEQUAL(hex2ba(hex_be, 'big'), a_be)
            self.assertEQUAL(hex2ba(hex_le, 'little'), a_le)
            self.assertEqual(ba2hex(a_be), hex_be)
            self.assertEqual(ba2hex(a_le), hex_le)
            # test simple encode / decode implementation
            self.assertEQUAL(self.hex2ba(hex_be, 'big'), a_be)
            self.assertEQUAL(self.hex2ba(hex_le, 'little'), a_le)
            self.assertEqual(self.ba2hex(a_be), hex_be)
            self.assertEqual(self.ba2hex(a_le), hex_le)

    def test_round_trip(self):
        for i in range(100):
            s = ''.join(choice(hexdigits) for _ in range(randint(0, 1000)))
            for default_endian in 'big', 'little':
                _set_default_endian(default_endian)
                a = hex2ba(s)
                self.assertEqual(len(a) % 4, 0)
                self.assertEqual(a.endian(), default_endian)
                t = ba2hex(a)
                self.assertEqual(t, s.lower())
                b = hex2ba(t, default_endian)
                self.assertEQUAL(a, b)
                # test simple encode / decode implementation
                self.assertEQUAL(a, self.hex2ba(t))
                self.assertEqual(t, self.ba2hex(a))


tests.append(TestsHexlify)

# ---------------------------------------------------------------------------

class TestsIntegerization(unittest.TestCase, Util):

    def test_ba2int(self):
        self.assertEqual(ba2int(bitarray('0')), 0)
        self.assertEqual(ba2int(bitarray('1')), 1)
        self.assertEqual(ba2int(bitarray('00101', 'big')), 5)
        self.assertEqual(ba2int(bitarray('00101', 'little')), 20)
        self.assertEqual(ba2int(frozenbitarray('11')), 3)
        self.assertRaises(ValueError, ba2int, bitarray())
        self.assertRaises(ValueError, ba2int, frozenbitarray())
        self.assertRaises(TypeError, ba2int, '101')
        a = bitarray('111')
        b = a.copy()
        self.assertEqual(ba2int(a), 7)
        # ensure original object wasn't altered
        self.assertEQUAL(a, b)

    def test_int2ba(self):
        self.assertEqual(int2ba(0), bitarray('0'))
        self.assertEqual(int2ba(1), bitarray('1'))
        self.assertEqual(int2ba(5), bitarray('101'))
        self.assertEQUAL(int2ba(6, endian='big'), bitarray('110', 'big'))
        self.assertEQUAL(int2ba(6, endian='little'),
                         bitarray('011', 'little'))
        self.assertRaises(TypeError, int2ba, 1.0)
        self.assertRaises(TypeError, int2ba, 1, 3.0)
        self.assertRaises(ValueError, int2ba, 1, 0)
        self.assertRaises(TypeError, int2ba, 1, 10, 123)
        self.assertRaises(ValueError, int2ba, 1, 10, 'asd')
        # signed integer requires length
        self.assertRaises(TypeError, int2ba, 100, signed=True)

    def test_signed(self):
        for s, i in [
                ('0',  0),
                ('1', -1),
                ('00',  0),
                ('10',  1),
                ('01', -2),
                ('11', -1),
                ('000',  0),
                ('100',  1),
                ('010',  2),
                ('110',  3),
                ('001', -4),
                ('101', -3),
                ('011', -2),
                ('111', -1),
                ('00000',   0),
                ('11110',  15),
                ('00001', -16),
                ('11111',  -1),
                ('000000000',    0),
                ('111111110',  255),
                ('000000001', -256),
                ('111111111',   -1),
                ('0000000000000000000000', 0),
                ('1001000011000000100010', 9 + 3 * 256 + 17 * 2 ** 16),
                ('1111111111111111111110', 2 ** 21 - 1),
                ('0000000000000000000001', -2 ** 21),
                ('1001000011000000100011', -2 ** 21
                                           + (9 + 3 * 256 + 17 * 2 ** 16)),
                ('1111111111111111111111', -1),
        ]:
            self.assertEqual(ba2int(bitarray(s, 'little'), signed=1), i)
            self.assertEqual(ba2int(bitarray(s[::-1], 'big'), signed=1), i)

            self.assertEQUAL(int2ba(i, len(s), 'little', signed=1),
                             bitarray(s, 'little'))
            self.assertEQUAL(int2ba(i, len(s), 'big', signed=1),
                             bitarray(s[::-1], 'big'))

    def test_int2ba_overflow(self):
        self.assertRaises(OverflowError, int2ba, -1)
        self.assertRaises(OverflowError, int2ba, -1, 4)

        self.assertRaises(OverflowError, int2ba, 128, 7)
        self.assertRaises(OverflowError, int2ba, 64, 7, signed=1)
        self.assertRaises(OverflowError, int2ba, -65, 7, signed=1)

        for n in range(1, 20):
            self.assertRaises(OverflowError, int2ba, 2 ** n, n)
            self.assertRaises(OverflowError, int2ba, 2 ** (n - 1), n,
                              signed=1)
            self.assertRaises(OverflowError, int2ba, -2 ** (n - 1) - 1, n,
                              signed=1)

    def test_int2ba_length(self):
        self.assertRaises(TypeError, int2ba, 0, 1.0)
        self.assertRaises(ValueError, int2ba, 0, 0)
        self.assertEqual(int2ba(5, length=6, endian='big'),
                         bitarray('000101'))
        for n in range(1, 100):
            ab = int2ba(1, n, 'big')
            al = int2ba(1, n, 'little')
            self.assertEqual(ab.endian(), 'big')
            self.assertEqual(al.endian(), 'little')
            self.assertEqual(len(ab), n),
            self.assertEqual(len(al), n)
            self.assertEqual(ab, bitarray((n - 1) * '0') + bitarray('1'))
            self.assertEqual(al, bitarray('1') + bitarray((n - 1) * '0'))

            ab = int2ba(0, n, 'big')
            al = int2ba(0, n, 'little')
            self.assertEqual(len(ab), n)
            self.assertEqual(len(al), n)
            self.assertEqual(ab, bitarray(n * '0', 'big'))
            self.assertEqual(al, bitarray(n * '0', 'little'))

            self.assertEqual(int2ba(2 ** n - 1), bitarray(n * '1'))
            self.assertEqual(int2ba(2 ** n - 1, endian='little'),
                             bitarray(n * '1'))
            for endian in 'big', 'little':
                self.assertEqual(int2ba(-1, n, endian, signed=True),
                                 bitarray(n * '1'))

    def test_explicit(self):
        _set_default_endian('big')
        for i, sa in [( 0,     '0'),    (1,         '1'),
                      ( 2,    '10'),    (3,        '11'),
                      (25, '11001'),  (265, '100001001'),
                      (3691038, '1110000101001000011110')]:
            ab = bitarray(sa, 'big')
            al = bitarray(sa[::-1], 'little')
            self.assertEQUAL(int2ba(i), ab)
            self.assertEQUAL(int2ba(i, endian='big'), ab)
            self.assertEQUAL(int2ba(i, endian='little'), al)
            self.assertEqual(ba2int(ab), ba2int(al), i)

    def check_round_trip(self, i):
        for endian in 'big', 'little':
            a = int2ba(i, endian=endian)
            self.assertEqual(a.endian(), endian)
            self.assertTrue(len(a) > 0)
            # ensure we have no leading zeros
            if a.endian == 'big':
                self.assertTrue(len(a) == 1 or a.index(1) == 0)
            self.assertEqual(ba2int(a), i)
            if i > 0:
                self.assertEqual(i.bit_length(), len(a))
            # add a few trailing / leading zeros to bitarray
            if endian == 'big':
                a = zeros(randint(0, 3), endian) + a
            else:
                a = a + zeros(randint(0, 3), endian)
            self.assertEqual(a.endian(), endian)
            self.assertEqual(ba2int(a), i)

    def test_many(self):
        for i in range(100):
            self.check_round_trip(i)
            self.check_round_trip(randint(0, 10 ** randint(3, 300)))

    @staticmethod
    def twos_complement(i, num_bits):
        # https://en.wikipedia.org/wiki/Two%27s_complement
        mask = 2 ** (num_bits - 1)
        return -(i & mask) + (i & ~mask)

    def test_random_signed(self):
        for a in self.randombitarrays(start=1):
            i = ba2int(a, signed=True)
            b = int2ba(i, len(a), a.endian(), signed=True)
            self.assertEQUAL(a, b)

            j = ba2int(a, signed=False)  # unsigned
            if i >= 0:
                self.assertEqual(i, j)

            self.assertEqual(i, self.twos_complement(j, len(a)))


tests.append(TestsIntegerization)

# ---------------------------------------------------------------------------

class TestsHuffman(unittest.TestCase):

    def test_simple(self):
        freq = {0: 10, 'as': 2, None: 1.6}
        code = huffman_code(freq)
        self.assertEqual(len(code), 3)
        self.assertEqual(len(code[0]), 1)
        self.assertEqual(len(code['as']), 2)
        self.assertEqual(len(code[None]), 2)

    def test_tiny(self):
        code = huffman_code({0: 0})
        self.assertEqual(len(code), 1)
        self.assertEqual(code, {0: bitarray()})

        code = huffman_code({0: 0, 1: 0})
        self.assertEqual(len(code), 2)
        for i in range(2):
            self.assertEqual(len(code[i]), 1)

    def test_endianness(self):
        freq = {'A': 10, 'B': 2, 'C': 5}
        for endian in 'big', 'little':
            code = huffman_code(freq, endian)
            self.assertEqual(len(code), 3)
            for v in code.values():
                self.assertEqual(v.endian(), endian)

    def test_wrong_arg(self):
        self.assertRaises(TypeError, huffman_code, [('a', 1)])
        self.assertRaises(TypeError, huffman_code, 123)
        self.assertRaises(TypeError, huffman_code, None)
        # cannot compare 'a' with 1
        self.assertRaises(TypeError, huffman_code, {'A': 'a', 'B': 1})
        self.assertRaises(ValueError, huffman_code, {})

    def check_tree(self, code):
        n = len(code)
        tree = decodetree(code)
        self.assertEqual(tree.todict(), code)
        # ensure tree has 2n-1 nodes (n symbol nodes and n-1 internal nodes)
        self.assertEqual(tree.nodes(), 2 * n - 1)

    def test_balanced(self):
        n = 6
        freq = {}
        for i in range(2 ** n):
            freq[i] = 1
        code = huffman_code(freq)
        self.assertEqual(len(code), 2 ** n)
        self.assertTrue(all(len(v) == n for v in code.values()))
        self.check_tree(code)

    def test_unbalanced(self):
        N = 27
        freq = {}
        for i in range(N):
            freq[i] = 2 ** i
        code = huffman_code(freq)
        self.assertEqual(len(code), N)
        for i in range(N):
            self.assertEqual(len(code[i]), N - (1 if i <= 1 else i))
        self.check_tree(code)

    def test_counter(self):
        message = 'the quick brown fox jumps over the lazy dog.'
        code = huffman_code(Counter(message))
        a = bitarray()
        a.encode(code, message)
        self.assertEqual(''.join(a.decode(code)), message)
        self.check_tree(code)

    def test_random_list(self):
        plain = [randint(0, 100) for _ in range(500)]
        code = huffman_code(Counter(plain))
        a = bitarray()
        a.encode(code, plain)
        self.assertEqual(a.decode(code), plain)
        self.check_tree(code)

    def test_random_freq(self):
        N = randint(2, 1000)
        # create Huffman code for N symbols
        code = huffman_code({i: random() for i in range(N)})
        self.check_tree(code)

tests.append(TestsHuffman)

# ---------------------------------------------------------------------------

def run(verbosity=1):
    import os
    import bitarray

    print('bitarray is installed in: %s' % os.path.dirname(bitarray.__file__))
    print('bitarray version: %s' % bitarray.__version__)
    print('Python version: %s' % sys.version)

    suite = unittest.TestSuite()
    for cls in tests:
        suite.addTest(unittest.makeSuite(cls))

    runner = unittest.TextTestRunner(verbosity=verbosity)
    return runner.run(suite)


if __name__ == '__main__':
    run()
