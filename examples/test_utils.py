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
        self.assertEqual(ba2hex(frozenbitarray('11000111')), b'c7')
        # length not multiple of 4
        self.assertRaises(ValueError, ba2hex, bitarray('10'))
        self.assertRaises(ValueError, ba2hex, bitarray('1011', 'little'))
        self.assertRaises(TypeError, ba2hex, '101')
        a = bitarray('1111')
        b = a.copy()
        self.assertEqual(ba2hex(a), b'f')
        # ensure original object wasn't altered
        self.assertEQUAL(a, b)

    def test_hex2ba(self):
        self.assertEqual(hex2ba(''), bitarray())
        for c in 'e', 'E', b'e', b'E':
            self.assertEqual(hex2ba(c), bitarray('1110'))
        self.assertRaises(Exception, hex2ba, '01a7x89')
        self.assertRaises(TypeError, hex2ba, 0)

    def test_explicit(self):
        for h, sa in [(b'',    ''),         (b'0',  '0000'),
                      (b'a',   '1010'),     (b'f',  '1111'),
                      (b'1a',  '00011010'), (b'2b', '00101011'),
                      (b'f7e', '111101111110')]:
            a = bitarray(sa)
            self.assertEqual(hex2ba(h), a)
            self.assertEqual(ba2hex(a), h)

    def test_round_trip(self):
        for i in range(100):
            s = ''.join(choice(hexdigits) for _ in range(randint(0, 1000)))
            t = ba2hex(hex2ba(s))
            self.assertEqual(t.decode(), s.lower())

    def test_round_trip2(self):
        for a in self.randombitarrays():
            if len(a) % 4 or a.endian() == 'little':
                self.assertRaises(ValueError, ba2hex, a)
            else:
                b = hex2ba(ba2hex(a))
                self.assertEqual(b, a)

tests.append(TestsHexlify)

# ---------------------------------------------------------------------------

class TestsIntegerization(unittest.TestCase, Util):

    def test_ba2int(self):
        self.assertEqual(ba2int(bitarray('0')), 0)
        self.assertEqual(ba2int(bitarray('1')), 1)
        self.assertEqual(ba2int(bitarray('00101')), 5)
        self.assertEqual(ba2int(frozenbitarray('11')), 3)
        self.assertRaises(ValueError, ba2int, bitarray())
        self.assertRaises(ValueError, ba2int, bitarray(endian='little'))
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
        self.assertRaises(ValueError, int2ba, -1)
        self.assertRaises(TypeError, int2ba, 1.0)

    def test_int2ba_length(self):
        self.assertRaises(TypeError, int2ba, 0, 1.0)
        self.assertRaises(ValueError, int2ba, 0, 0)
        self.assertEqual(int2ba(5, length=6), bitarray('000101'))
        self.assertRaises(OverflowError, int2ba, 3, 1)
        for n in range(1, 100):
            a = int2ba(1, n)
            self.assertEqual(len(a), n)
            self.assertEqual(a, bitarray((n - 1) * '0') + bitarray('1'))
            a = int2ba(0, n)
            self.assertEqual(len(a), n)
            self.assertEqual(a, bitarray(n * '0'))
            self.assertRaises(OverflowError, int2ba, 2 ** n, n)
            self.assertEqual(int2ba(2 ** n - 1), bitarray(n * '1'))
        for n in range(9, 100):
            a = int2ba(269, n)
            self.assertEqual(len(a), n)

    def test_explicit(self):
        for i, sa in [( 0,     '0'),    (1,         '1'),
                      ( 2,    '10'),    (3,        '11'),
                      (25, '11001'),  (265, '100001001'),
                      (3691038, '1110000101001000011110')]:
            a = bitarray(sa)
            self.assertEqual(int2ba(i), a)
            self.assertEqual(ba2int(a), i)
            if i == 0 or i >= 512:
                continue
            for n in range(9, 32):
                b = int2ba(i, n)
                self.assertEqual(len(b), n)
                f = b.index(1)
                self.assertEqual(b[:f], bitarray(f * '0'))
                self.assertEqual(b[f:], a)

    def check_round_trip(self, i):
        a = int2ba(i)
        self.assertTrue(len(a) > 0)
        # ensure we have no leading zeros
        self.assertTrue(len(a) == 1 or a.index(1) == 0)
        self.assertEqual(ba2int(a), i)
        if i > 0 and sys.version_info[:2] >= (2, 7):
            self.assertEqual(i.bit_length(), len(a))
        # add a few leading zeros to bitarray
        a = bitarray(randint(0, 3) * '0') + a
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
