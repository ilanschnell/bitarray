"""
Tests for utils module
"""
import os
import sys
import unittest
from string import hexdigits
from random import choice, randint

from bitarray import bitarray, frozenbitarray, bits2bytes
from bitarray.test_bitarray import Util

from bitarray.utils import (zeros, rindex, strip, count_n,
                            count_and, count_or, count_xor, subset,
                            ba2hex, hex2ba, ba2int, int2ba)


tests = []

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

class TestsHelpers(unittest.TestCase, Util):

    def test_rindex(self):
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

            for v in 0, 1:
                self.assertRaises(ValueError, rindex,
                                  bitarray(0, endian), v)
            self.assertRaises(ValueError, rindex,
                              bitarray('000', endian), 1)
            self.assertRaises(ValueError, rindex,
                              bitarray('11111', endian), 0)

    def test_rindex2(self):
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

    def test_rindex3(self):
        for _ in range(100):
            n = randint(1, 100000)
            v = randint(0, 1)
            a = bitarray(n)
            a.setall(1 - v)
            lst = [randint(0, n - 1) for _ in range(100)]
            for i in lst:
                a[i] = v
            self.assertEqual(rindex(a, v), max(lst))

    def test_rindex4(self):
        for _ in range(100):
            N = randint(1, 10000)
            a = bitarray(N)
            a.setall(0)
            a[randint(0, N - 1)] = 1
            self.assertEqual(rindex(a), a.index(1))

    def test_strip1(self):
        self.assertRaises(TypeError, strip, bitarray(), 123)
        self.assertRaises(ValueError, strip, bitarray(), 'up')
        for endian in 'big', 'little':
            a = bitarray('00010110000', endian)
            self.assertEQUAL(strip(a), bitarray('0001011', endian))
            self.assertEQUAL(strip(a, 'left'), bitarray('10110000', endian))
            self.assertEQUAL(strip(a, 'both'), bitarray('1011', endian))

        for mode in 'left', 'right', 'both':
            self.assertEqual(strip(bitarray('000'), mode), bitarray())
            self.assertEqual(strip(bitarray(), mode), bitarray())

    def test_strip2(self):
        for a in self.randombitarrays():
            b = a.copy()
            s = a.to01()
            self.assertEqual(strip(a, 'left'), bitarray(s.lstrip('0')))
            self.assertEqual(strip(a, 'right'), bitarray(s.rstrip('0')))
            self.assertEqual(strip(a, 'both'), bitarray(s.strip('0')))
            self.assertEQUAL(a, b)

    def test_strip_both(self):
        for _ in range(100):
            N = randint(1, 10000)
            a = bitarray(N)
            a.setall(0)
            a[randint(0, N - 1)] = 1
            self.assertEqual(strip(a, 'both'), bitarray('1'))

    def check_result(self, a, n, i):
        self.assertEqual(a.count(1, 0, i), n)
        if i:
            self.assertTrue(a[i - 1])

    def test_count_n1(self):
        a = bitarray('111110111110111110111110011110111110111110111000')
        b = a.copy()
        self.assertEqual(len(a), 48)
        self.assertEqual(a.count(), 37)
        self.assertEqual(count_n(a, 0), 0)
        self.assertEqual(count_n(a, 20), 23)
        self.assertEqual(count_n(a, 37), 45)
        self.assertRaises(ValueError, count_n, a, -1) # n < 0
        self.assertRaises(ValueError, count_n, a, 38) # n > a.count()
        self.assertRaises(ValueError, count_n, a, 49) # n > len(a)
        self.assertRaises(TypeError, count_n, a, "7")
        for n in range(0, 37):
            i = count_n(a, n)
            self.check_result(a, n, i)
            self.assertEqual(a[:i].count(), n)
        self.assertEQUAL(a, b)

    def test_count_n2(self):
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

    def test_count_n3(self):
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

    def test_count_n4(self):
        for a in self.randombitarrays():
            n = a.count() // 2
            i = count_n(a, n)
            self.check_result(a, n, i)

tests.append(TestsHelpers)

# ---------------------------------------------------------------------------

class TestsBitwiseCount(unittest.TestCase, Util):

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

    def test_subset(self):
        a = bitarray('0101')
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

    def test_subset_True(self):
        for a, b in [('', ''), ('0', '1'), ('0', '0'), ('1', '1'),
                     ('000', '111'), ('0101', '0111'),
                     ('000010111', '010011111')]:
            a, b = bitarray(a), bitarray(b)
            self.assertTrue(subset(a, b) is True)
            self.assertTrue(self.subset_simple(a, b) is True)

    def test_subset_False(self):
        for a, b in [('1', '0'), ('1101', '0111'),
                     ('0000101111', '0100111011')]:
            a, b = bitarray(a), bitarray(b)
            self.assertTrue(subset(a, b) is False)
            self.assertTrue(self.subset_simple(a, b) is False)

    def test_many(self):
        for a in self.randombitarrays(start=1):
            b = a.copy()
            # we set one random bit in b to 1, so a is always a subset of b
            b[randint(0, len(a)-1)] = 1
            self.assertTrue(subset(a, b))
            # but b in not always a subset of a
            self.assertEqual(subset(b, a), self.subset_simple(b, a))
            # we set all bits in a, which ensures that b is a subset of a
            a.setall(1)
            self.assertTrue(subset(b, a))

tests.append(TestsSubset)

# ---------------------------------------------------------------------------

class TestsHexlify(unittest.TestCase, Util):

    def test_ba2hex(self):
        self.assertEqual(ba2hex(bitarray(0, 'big')), b'')
        self.assertEqual(ba2hex(bitarray('1110', 'big')), b'e')
        self.assertEqual(ba2hex(bitarray('00000001', 'big')), b'01')
        self.assertEqual(ba2hex(bitarray('10000000', 'big')), b'80')
        self.assertEqual(ba2hex(frozenbitarray('11000111', 'big')), b'c7')
        # length not multiple of 4
        self.assertRaises(ValueError, ba2hex, bitarray('10'))
        self.assertRaises(ValueError, ba2hex, bitarray(endian='little'))
        self.assertRaises(TypeError, ba2hex, '101')

        for n in range(7):
            a = bitarray(n * '1111', 'big')
            b = a.copy()
            self.assertEqual(ba2hex(a), n * b'f')
            # ensure original object wasn't altered
            self.assertEQUAL(a, b)

    def test_hex2ba(self):
        self.assertEqual(hex2ba(''), bitarray())
        for c in 'e', 'E', b'e', b'E':
            a = hex2ba(c)
            self.assertEqual(a.to01(), '1110')
            self.assertEqual(a.endian(), 'big')
        self.assertEQUAL(hex2ba('01'), bitarray('00000001', 'big'))
        self.assertRaises(Exception, hex2ba, '01a7x89')
        self.assertRaises(TypeError, hex2ba, 0)

    def test_explicit(self):
        for h, bs in [(b'',    ''),
                      (b'0',   '0000'),
                      (b'a',   '1010'),
                      (b'f',   '1111'),
                      (b'1a',  '00011010'),
                      (b'2b',  '00101011'),
                      (b'4c1', '010011000001'),
                      (b'a7d', '101001111101')]:
            a = bitarray(bs, 'big')
            self.assertEQUAL(hex2ba(h), a)
            self.assertEqual(ba2hex(a), h)

    def test_round_trip(self):
        for i in range(100):
            s = ''.join(choice(hexdigits) for _ in range(randint(0, 1000)))
            t = ba2hex(hex2ba(s))
            self.assertEqual(t.decode(), s.lower())

    def test_round_trip2(self):
        for a in self.randombitarrays():
            if len(a) % 4 or a.endian() != 'big':
                self.assertRaises(ValueError, ba2hex, a)
                continue
            b = hex2ba(ba2hex(a))
            self.assertEQUAL(b, a)

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
        self.assertEQUAL(int2ba(6), bitarray('110', 'big'))
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
            self.assertEqual(ab, bitarray(n * '0', 'big'))
            self.assertEqual(al, bitarray(n * '0', 'little'))

            self.assertRaises(OverflowError, int2ba, 2 ** n, n, 'big')
            self.assertRaises(OverflowError, int2ba, 2 ** n, n, 'little')
            self.assertEqual(int2ba(2 ** n - 1), bitarray(n * '1'))
            self.assertEqual(int2ba(2 ** n - 1, endian='little'),
                             bitarray(n * '1'))

    def test_explicit(self):
        for i, sa in [( 0,     '0'),    (1,         '1'),
                      ( 2,    '10'),    (3,        '11'),
                      (25, '11001'),  (265, '100001001'),
                      (3691038, '1110000101001000011110')]:
            ab = bitarray(sa, 'big')
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
