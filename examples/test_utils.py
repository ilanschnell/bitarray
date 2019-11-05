"""
Tests for utils module
"""
import sys
import unittest
from string import hexdigits
from random import choice, randint

from bitarray import bitarray
from bitarray.test_bitarray import Util

from utils import frozenbitarray, zeros, ba2hex, hex2ba, ba2int, int2ba


tests = []

# ---------------------------------------------------------------------------

class TestsFrozenbitarray(unittest.TestCase, Util):

    def test_init(self):
        a = frozenbitarray('110')
        self.assertEqual(a, bitarray('110'))
        self.assertEqual(a.endian(), 'big')
        self.assertEqual(a.to01(), '110')

    def test_init_bitarray(self):
        for a in self.randombitarrays():
            b = frozenbitarray(a)
            self.assertFalse(b is a)
            self.assertEqual(b, a)
            self.assertEqual(b.endian(), a.endian())
            c = frozenbitarray(b)
            self.assertEqual(c, b)
            self.assertFalse(c is b)
            self.assertEqual(c.endian(), a.endian())
            self.assertEqual(hash(c), hash(b))

    def test_repr(self):
        a = frozenbitarray()
        self.assertEqual(repr(a), "frozenbitarray()")
        self.assertEqual(str(a), "frozenbitarray()")

    def test_imutable(self):
        a = frozenbitarray('111')
        self.assertRaises(TypeError, a.append, True)
        self.assertRaises(TypeError, a.__setitem__, 0, 0)
        self.assertRaises(TypeError, a.__delitem__, 0)

    def test_dictkey(self):
        a = frozenbitarray('01')
        b = frozenbitarray('1001')
        d = {a: 123, b: 345}
        self.assertEqual(d[frozenbitarray('01')], 123)
        self.assertEqual(d[frozenbitarray(b)], 345)

    def test_mix(self):
        a = bitarray('110')
        b = frozenbitarray('0011')
        self.assertEqual(a + b, bitarray('1100011'))
        a.extend(b)
        self.assertEqual(a, bitarray('1100011'))

tests.append(TestsFrozenbitarray)

# ---------------------------------------------------------------------------

class TestsZeros(unittest.TestCase):

    def test_init(self):
        a = zeros(0)
        self.assertEqual(a, bitarray(''))
        self.assertEqual(a.endian(), 'big')
        self.assertRaises(TypeError, zeros, 1.0)
        for n in range(100):
            a = zeros(n)
            self.assertEqual(a, bitarray(n * '0'))
        self.assertRaises(TypeError, zeros) # no argument

        # wrong arguments
        self.assertRaises(TypeError, zeros, '')
        self.assertRaises(TypeError, zeros, bitarray())
        self.assertRaises(TypeError, zeros, [])
        self.assertRaises(ValueError, zeros, -1)

        self.assertRaises(TypeError, zeros, 0, 1) # endian not string
        self.assertRaises(ValueError, zeros, 0, 'foo') # endian wrong string

    def test_endian(self):
        for endian in 'big', 'little':
            a = zeros(1, endian)
            self.assertEqual(a, bitarray('0'))
            self.assertEqual(a.endian(), endian)
            a = zeros(1, endian=endian)
            self.assertEqual(a, bitarray('0'))
            self.assertEqual(a.endian(), endian)

tests.append(TestsZeros)

# ---------------------------------------------------------------------------

class TestsHexlify(unittest.TestCase, Util):

    def test_ba2hex(self):
        self.assertEqual(ba2hex(bitarray()), b'')
        self.assertEqual(ba2hex(bitarray('1110')), b'e')
        self.assertEqual(ba2hex(bitarray('00000001')), b'01')
        self.assertEqual(ba2hex(bitarray('10000000', 'little')), b'01')
        self.assertEqual(ba2hex(frozenbitarray('11000111')), b'c7')
        self.assertEqual(ba2hex(bitarray('11100011', 'little')), b'c7')
        self.assertEqual(ba2hex(bitarray('0111', 'little')), b'e')
        # length not multiple of 4
        self.assertRaises(ValueError, ba2hex, bitarray('10'))
        self.assertRaises(TypeError, ba2hex, '101')

        for n in range(7):
            for endian in 'big', 'little':
                a = bitarray(n * '1111', endian)
                b = a.copy()
                self.assertEqual(ba2hex(a), n * b'f')
                # ensure original object wasn't altered
                self.assertEQUAL(a, b)

    def test_hex2ba(self):
        self.assertEqual(hex2ba(''), bitarray())
        for c in 'e', 'E', b'e', b'E':
            self.assertEQUAL(hex2ba(c), bitarray('1110'))
            self.assertEQUAL(hex2ba(c, 'little'), bitarray('0111', 'little'))
        self.assertEQUAL(hex2ba('01'), bitarray('00000001'))
        self.assertEQUAL(hex2ba('01', 'little'),
                                       bitarray('10000000', 'little'))
        self.assertRaises(Exception, hex2ba, '01a7x89')
        self.assertRaises(TypeError, hex2ba, 0)
        self.assertRaises(TypeError, hex2ba, 'af', 1)
        self.assertRaises(ValueError, hex2ba, 'af', 'nkj')

    def test_explicit(self):
        for hb, hl, bs in [(b'',    b'',    ''),
                           (b'0',   b'0',   '0000'),
                           (b'a',   b'5',   '1010'),
                           (b'f',   b'f',   '1111'),
                           (b'1a',  b'58',  '00011010'),
                           (b'2b',  b'd4',  '00101011'),
                           (b'4c1', b'328', '010011000001'),
                           (b'a7d', b'e5b', '101001111101')]:
            ab = bitarray(bs)
            self.assertEQUAL(hex2ba(hb), ab)
            self.assertEqual(ba2hex(ab), hb)
            al = bitarray(bs, 'little')
            self.assertEQUAL(hex2ba(hl, endian='little'), al)
            self.assertEqual(ba2hex(al), hl)

    def test_round_trip(self):
        for i in range(100):
            s = ''.join(choice(hexdigits) for _ in range(randint(0, 1000)))
            t = ba2hex(hex2ba(s))
            self.assertEqual(t.decode(), s.lower())
            t = ba2hex(hex2ba(s, 'little'))
            self.assertEqual(t.decode(), s.lower())

    def test_round_trip2(self):
        for a in self.randombitarrays():
            if len(a) % 4:
                self.assertRaises(ValueError, ba2hex, a)
                continue
            b = hex2ba(ba2hex(a), a.endian())
            self.assertEQUAL(b, a)

tests.append(TestsHexlify)

# ---------------------------------------------------------------------------

class TestsIntegerization(unittest.TestCase, Util):

    def test_ba2int(self):
        self.assertEqual(ba2int(bitarray('0')), 0)
        self.assertEqual(ba2int(bitarray('1')), 1)
        self.assertEqual(ba2int(bitarray('00101')), 5)
        self.assertEqual(ba2int(bitarray('00101', 'little')), 20)
        self.assertEqual(ba2int(frozenbitarray('11')), 3)
        self.assertRaises(ValueError, ba2int, bitarray())
        self.assertRaises(ValueError, ba2int, frozenbitarray())
        self.assertRaises(TypeError, ba2hex, '101')
        a = bitarray('111')
        b = a.copy()
        self.assertEqual(ba2int(a), 7)
        # ensure original object wasn't altered
        self.assertEQUAL(a, b)

    def test_int2ba(self):
        self.assertEqual(int2ba(0), bitarray('0'))
        self.assertEqual(int2ba(1), bitarray('1'))
        self.assertEqual(int2ba(5), bitarray('101'))
        self.assertEQUAL(int2ba(6), bitarray('110'))
        self.assertEQUAL(int2ba(6, endian='little'),
                         bitarray('011', 'little'))
        self.assertRaises(ValueError, int2ba, -1)
        self.assertRaises(TypeError, int2ba, 1.0)
        self.assertRaises(TypeError, int2ba, 1, 3.0)
        self.assertRaises(ValueError, int2ba, 1, 0)
        self.assertRaises(TypeError, int2ba, 1, 10, 123)
        self.assertRaises(ValueError, int2ba, 1, 10, 'asd')

    def test_int2ba_length(self):
        self.assertRaises(TypeError, int2ba, 0, 1.0)
        self.assertRaises(ValueError, int2ba, 0, 0)
        self.assertEqual(int2ba(5, length=6), bitarray('000101'))
        self.assertRaises(OverflowError, int2ba, 3, 1)
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
            self.assertEqual(ab, al, bitarray(n * '0'))
            self.assertRaises(OverflowError, int2ba, 2 ** n, n)
            self.assertRaises(OverflowError, int2ba, 2 ** n, n, 'little')
            self.assertEqual(int2ba(2 ** n - 1), bitarray(n * '1'))

    def test_explicit(self):
        for i, sa in [( 0,     '0'),    (1,         '1'),
                      ( 2,    '10'),    (3,        '11'),
                      (25, '11001'),  (265, '100001001'),
                      (3691038, '1110000101001000011110')]:
            ab = bitarray(sa)
            al = bitarray(sa[::-1], 'little')
            self.assertEQUAL(int2ba(i), ab)
            self.assertEQUAL(int2ba(i, endian='little'), al)
            self.assertEqual(ba2int(ab), ba2int(al), i)
            if i == 0 or i >= 512:
                continue
            for n in range(9, 32):
                for endian in 'big', 'little':
                    a = int2ba(i, length=n, endian=endian)
                    self.assertEqual(a.endian(), endian)
                    self.assertEqual(len(a), n)
                    if endian == 'big':
                        f = a.index(1)
                        self.assertEqual(a[:f], bitarray(f * '0'))
                        self.assertEqual(a[f:], ab)

    def check_round_trip(self, i):
        for endian in 'big', 'little':
            a = int2ba(i, endian=endian)
            self.assertEqual(a.endian(), endian)
            self.assertTrue(len(a) > 0)
            # ensure we have no leading zeros
            if a.endian == 'big':
                self.assertTrue(len(a) == 1 or a.index(1) == 0)
            self.assertEqual(ba2int(a), i)
            if i > 0 and sys.version_info[:2] >= (2, 7):
                self.assertEqual(i.bit_length(), len(a))
            # add a few / trailing leading zeros to bitarray
            if endian == 'big':
                a = zeros(randint(0, 3), endian) + a
            else:
                a = a + zeros(randint(0, 3), endian)
            self.assertEqual(a.endian(), endian)
            self.assertEqual(ba2int(a), i)

    def test_many(self):
        for i in range(1000):
            self.check_round_trip(i)
            self.check_round_trip(randint(0, 10 ** randint(3, 300)))


tests.append(TestsIntegerization)

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
