"""
Tests for bitarray

Author: Ilan Schnell
"""
from __future__ import absolute_import

import os
import sys
import unittest
import tempfile
import shutil
from random import randint

is_py3k = bool(sys.version_info[0] == 3)

# imports needed inside tests
import copy
import pickle
import itertools

try:
    import shelve, hashlib
except ImportError:
    shelve = hashlib = None

if is_py3k:
    from io import BytesIO
    unicode = str
else:
    from cStringIO import StringIO as BytesIO
    range = xrange


from bitarray import (bitarray, frozenbitarray, bits2bytes, decodetree,
                      get_default_endian, _set_default_endian,
                      _sysinfo, __version__)

tests = []


class Util(object):

    @staticmethod
    def is_heavy_test():
        return int(os.getenv('TEST_HEAVY', 0))

    @staticmethod
    def randomendian():
        return ['little', 'big'][randint(0, 1)]

    @staticmethod
    def randombitarrays(start=0):
        nb1 = 250 if Util.is_heavy_test() else 25
        nb2 = 10 if Util.is_heavy_test() else 1
        lst = list(range(start, nb1)) + [randint(1000, 2000) for _ in range(nb2)]
        for n in lst:
            yield Util.randombitarray(n)

    @staticmethod
    def randombitarray(n=None, endian=None):
        n = n if n else randint(1000, 2000)
        endian = endian if endian else Util.randomendian()
        a = bitarray(endian=endian)
        a.frombytes(os.urandom(bits2bytes(n)))
        del a[n:]
        return a

    @staticmethod
    def randomlists():
        for n in list(range(25)) + [randint(1000, 2000)]:
            yield [bool(randint(0, 1)) for d in range(n)]

    @staticmethod
    def rndsliceidx(length):
        if randint(0, 1):
            return None
        else:
            return randint(-length-5, length+5)

    @staticmethod
    def other_endian(endian):
        t = {'little': 'big',
             'big': 'little'}
        return t[endian]

    @staticmethod
    def slicelen(s, length):
        assert isinstance(s, slice)
        start, stop, step = s.indices(length)
        slicelength = (stop - start + (1 if step < 0 else -1)) // step + 1
        if slicelength < 0:
            slicelength = 0
        return slicelength

    def check_obj(self, a):
        self.assertEqual(repr(type(a)), "<%s 'bitarray.bitarray'>" %
                         ('class' if is_py3k else 'type'))
        unused = 8 * a.buffer_info()[1] - len(a)
        self.assertTrue(0 <= unused < 8)
        self.assertEqual(unused, a.buffer_info()[3])

    def assertEQUAL(self, a, b):
        self.assertEqual(a, b)
        self.assertEqual(a.endian(), b.endian())
        self.check_obj(a)
        self.check_obj(b)

    def assertStopIteration(self, it):
        self.assertRaises(StopIteration, next, it)

    def assertRaisesMessage(self, excClass, msg, callable, *args, **kwargs):
        try:
            callable(*args, **kwargs)
        except excClass as e:
            if msg != str(e):
                raise AssertionError("message: %s\n got: %s" % (msg, e))

# ---------------------------------------------------------------------------

class TestsModuleFunctions(unittest.TestCase, Util):

    def test_version_string(self):
        # the version string is not a function, but test it here anyway
        self.assertIsInstance(__version__, str)

    def test_set_default_endian(self):
        self.assertRaises(TypeError, _set_default_endian, 0)
        self.assertRaises(TypeError, _set_default_endian, 'little', 0)
        self.assertRaises(ValueError, _set_default_endian, 'foo')
        for default_endian in 'big', 'little', u'big', u'little':
            _set_default_endian(default_endian)
            a = bitarray()
            self.assertEqual(a.endian(), default_endian)
            for x in None, 0, 64, '10111', [1, 0]:
                a = bitarray(x)
                self.assertEqual(a.endian(), default_endian)

            for endian in 'big', 'little':
                a = bitarray(endian=endian)
                self.assertEqual(a.endian(), endian)

            # make sure that calling _set_default_endian wrong does not
            # change the default endianness
            self.assertRaises(ValueError, _set_default_endian, 'foobar')
            self.assertEqual(bitarray().endian(), default_endian)

    def test_get_default_endian(self):
        # takes no arguments
        self.assertRaises(TypeError, get_default_endian, 'big')
        for default_endian in 'big', 'little':
            _set_default_endian(default_endian)
            endian = get_default_endian()
            self.assertEqual(endian, default_endian)
            self.assertIsInstance(endian, str)

    def test_bits2bytes(self):
        for arg in 'foo', [], None, {}, 187.0, -4.0:
            self.assertRaises(TypeError, bits2bytes, arg)

        self.assertRaises(TypeError, bits2bytes)
        self.assertRaises(TypeError, bits2bytes, 1, 2)

        self.assertRaises(ValueError, bits2bytes, -1)
        self.assertRaises(ValueError, bits2bytes, -924)

        self.assertEqual(bits2bytes(0), 0)
        for n in range(1, 100):
            m = bits2bytes(n)
            self.assertEqual(m, (n - 1) // 8 + 1)
            self.assertIsInstance(m, int)

        for n, m in [(0, 0), (1, 1), (2, 1), (7, 1), (8, 1), (9, 2),
                     (10, 2), (15, 2), (16, 2), (64, 8), (65, 9),
                     (2**31, 2**28), (2**32, 2**29), (2**34, 2**31),
                     (2**34+793, 2**31+100), (2**35-8, 2**32-1),
                     (2**62, 2**59), (2**63-8, 2**60-1)]:
            self.assertEqual(bits2bytes(n), m)


tests.append(TestsModuleFunctions)

# ---------------------------------------------------------------------------

class CreateObjectTests(unittest.TestCase, Util):

    def test_noInitializer(self):
        a = bitarray()
        self.assertEqual(len(a), 0)
        self.assertEqual(a.tolist(), [])
        self.check_obj(a)

    def test_endian(self):
        a = bitarray(endian='little')
        a.frombytes(b'ABC')
        self.assertEqual(a.endian(), 'little')
        self.assertIsInstance(a.endian(), str)
        self.check_obj(a)

        b = bitarray(endian='big')
        b.frombytes(b'ABC')
        self.assertEqual(b.endian(), 'big')
        self.assertIsInstance(a.endian(), str)
        self.check_obj(b)

        self.assertNotEqual(a, b)
        self.assertEqual(a.tobytes(), b.tobytes())

    def test_endian_default(self):
        _set_default_endian('big')
        a_big = bitarray()
        _set_default_endian('little')
        a_little = bitarray()
        _set_default_endian('big')

        self.assertEqual(a_big.endian(), 'big')
        self.assertEqual(a_little.endian(), 'little')

    def test_endian_wrong(self):
        self.assertRaises(TypeError, bitarray.__new__, bitarray, endian=0)
        self.assertRaises(ValueError, bitarray.__new__, bitarray, endian='')
        self.assertRaisesMessage(
            ValueError,
            "bit endianness must be either 'little' or 'big', got: 'foo'",
            bitarray.__new__, bitarray, endian='foo')
        self.assertRaisesMessage(TypeError,
                                 "'ellipsis' object is not iterable",
                                 bitarray.__new__, bitarray, Ellipsis)

    def test_integers(self):
        for n in range(50):
            a = bitarray(n)
            self.assertEqual(len(a), n)
            self.check_obj(a)

            a = bitarray(int(n))
            self.assertEqual(len(a), n)
            self.check_obj(a)

        self.assertRaises(ValueError, bitarray.__new__, bitarray, -1)
        self.assertRaises(ValueError, bitarray.__new__, bitarray, -924)

    def test_list(self):
        lst = ['foo', None, [1], {}]
        a = bitarray(lst)
        self.assertEqual(a.tolist(), [True, False, True, False])
        self.check_obj(a)

        for n in range(50):
            lst = [bool(randint(0, 1)) for d in range(n)]
            a = bitarray(lst)
            self.assertEqual(a.tolist(), lst)
            self.check_obj(a)

    def test_tuple(self):
        tup = ('', True, [], {1:2})
        a = bitarray(tup)
        self.assertEqual(a.tolist(), [False, True, False, True])
        self.check_obj(a)

        for n in range(50):
            lst = [bool(randint(0, 1)) for d in range(n)]
            a = bitarray(tuple(lst))
            self.assertEqual(a.tolist(), lst)
            self.check_obj(a)

    def test_iter1(self):
        for n in range(50):
            lst = [bool(randint(0, 1)) for d in range(n)]
            a = bitarray(iter(lst))
            self.assertEqual(a.tolist(), lst)
            self.check_obj(a)

    def test_iter2(self):
        for lst in self.randomlists():
            def foo():
                for x in lst:
                    yield x
            a = bitarray(foo())
            self.assertEqual(a, bitarray(lst))
            self.check_obj(a)

    def test_iter3(self):
        a = bitarray(itertools.repeat(False, 10))
        self.assertEqual(a, bitarray(10 * '0'))
        # Note that the through value of '0' is True: bool('0') -> True
        a = bitarray(itertools.repeat('0', 10))
        self.assertEqual(a, bitarray(10 * '1'))

    def test_range(self):
        a = bitarray(range(-3, 3))
        self.assertEqual(a, bitarray('111011'))

    def test_01(self):
        a = bitarray('0010111')
        self.assertEqual(a.tolist(), [0, 0, 1, 0, 1, 1, 1])
        self.check_obj(a)

        for n in range(50):
            lst = [bool(randint(0, 1)) for d in range(n)]
            s = ''.join([['0', '1'][x] for x in lst])
            a = bitarray(s)
            self.assertEqual(a.tolist(), lst)
            self.check_obj(a)

        self.assertRaises(ValueError, bitarray.__new__, bitarray, '01012100')

    def test_rawbytes(self):  # this representation is used for pickling
        for s, r in [(b'\x00', ''), (b'\x07\xff', '1'), (b'\x03\xff', '11111'),
                     (b'\x01\x87\xda', '10000111' '1101101')]:
            self.assertEqual(bitarray(s, endian='big'),
                             bitarray(r))

        for i in range(1, 8):
            self.assertRaises(ValueError, bitarray.__new__,
                              bitarray, bytes(bytearray([i])))

    def test_bitarray_simple(self):
        for n in range(10):
            a = bitarray(n)
            b = bitarray(a)
            self.assertFalse(a is b)
            self.assertEQUAL(a, b)

    def test_bitarray_endian(self):
        # Test creating a new bitarray with different endianness from an
        # existing bitarray.
        for endian in 'little', 'big':
            a = bitarray(endian=endian)
            b = bitarray(a)
            self.assertFalse(a is b)
            self.assertEQUAL(a, b)

            endian2 = self.other_endian(endian)
            c = bitarray(a, endian2)
            self.assertEqual(c.endian(), endian2)
            self.assertEqual(a, c)  # but only because they are empty

            # Even though the byte representation will be the same,
            # the bitarrays are not equal.
            a = bitarray('11001000' '11110000', endian)
            self.assertEqual(len(a) % 8, 0)
            c = bitarray(a, endian2)
            # This is only equal because the size of the bitarray is a
            # multiple of 8, and unused bits are not set (which changes
            # the byte representation).
            self.assertEqual(a.tobytes(), c.tobytes())
            self.assertNotEqual(a.endian(), c.endian())
            self.assertNotEqual(a, c)

    def test_bitarray_endianness(self):
        a = bitarray('11100001', endian='little')
        b = bitarray(a, endian='big')
        self.assertNotEqual(a, b)
        self.assertEqual(a.tobytes(), b.tobytes())

        b.bytereverse()
        self.assertEqual(a, b)
        self.assertNotEqual(a.tobytes(), b.tobytes())

        c = bitarray('11100001', endian='big')
        self.assertEqual(a, c)

    def test_create_empty(self):
        for x in None, 0, '', list(), tuple(), set(), dict():
            a = bitarray(x)
            self.assertEqual(len(a), 0)
            self.assertEQUAL(a, bitarray())

    def test_wrong_args(self):
        # wrong types
        for x in False, True, Ellipsis, slice(0), 0.0, 0 + 0j:
            self.assertRaises(TypeError, bitarray.__new__, bitarray, x)
        # wrong values
        for x in -1, 'A':
            self.assertRaises(ValueError, bitarray.__new__, bitarray, x)
        # test second (endian) argument
        self.assertRaises(TypeError, bitarray.__new__, bitarray, 0, None)
        self.assertRaises(TypeError, bitarray.__new__, bitarray, 0, 0)
        self.assertRaises(ValueError, bitarray.__new__, bitarray, 0, 'foo')
        # too many args
        self.assertRaises(TypeError, bitarray.__new__, bitarray, 0, 'big', 0)

tests.append(CreateObjectTests)

# ---------------------------------------------------------------------------

class ToObjectsTests(unittest.TestCase, Util):

    def test_numeric(self):
        a = bitarray()
        self.assertRaises(Exception, int, a)
        self.assertRaises(Exception, float, a)
        self.assertRaises(Exception, complex, a)

    def test_list(self):
        for a in self.randombitarrays():
            self.assertEqual(list(a), a.tolist())

    def test_tuple(self):
        for a in self.randombitarrays():
            self.assertEqual(tuple(a), tuple(a.tolist()))


tests.append(ToObjectsTests)

# ---------------------------------------------------------------------------

class MetaDataTests(unittest.TestCase, Util):

    def test_buffer_info1(self):
        a = bitarray(13, endian='little')
        self.assertEqual(a.buffer_info()[1:4], (2, 'little', 3))

        a = bitarray()
        self.assertRaises(TypeError, a.buffer_info, 42)

        bi = a.buffer_info()
        self.assertIsInstance(bi, tuple)
        self.assertEqual(len(bi), 5)
        self.assertIsInstance(bi[0], int)

    def test_buffer_info2(self):
        for endian in 'big', 'little':
            for n in range(50):
                bi = bitarray(n, endian).buffer_info()
                self.assertEqual(bi[1], bits2bytes(n))  # bytes
                self.assertEqual(bi[2], endian)         # endianness
                self.assertEqual(bi[3], 8 * bi[1] - n)  # unused
                self.assertTrue(bi[4] >= bi[1])         # allocated

    def test_endian(self):
        for endian in 'big', 'little':
            a = bitarray(endian=endian)
            self.assertEqual(a.endian(), endian)

        a = bitarray(endian='big')
        self.assertEqual(a.endian(), 'big')

    def test_len(self):
        for n in range(100):
            a = bitarray(n)
            self.assertEqual(len(a), n)


tests.append(MetaDataTests)

# ---------------------------------------------------------------------------

class SliceTests(unittest.TestCase, Util):

    def test_getitem_1(self):
        a = bitarray()
        self.assertRaises(IndexError, a.__getitem__,  0)
        a.append(True)
        self.assertEqual(a[0], True)
        self.assertEqual(a[-1], True)
        self.assertRaises(IndexError, a.__getitem__,  1)
        self.assertRaises(IndexError, a.__getitem__, -2)
        a.append(False)
        self.assertEqual(a[1], False)
        self.assertEqual(a[-1], False)
        self.assertRaises(IndexError, a.__getitem__,  2)
        self.assertRaises(IndexError, a.__getitem__, -3)

    def test_getitem_2(self):
        a = bitarray('1100010')
        for i, b in enumerate(a):
            self.assertEqual(a[i], b)
            self.assertEqual(a[i - 7], b)
        self.assertRaises(IndexError, a.__getitem__,  7)
        self.assertRaises(IndexError, a.__getitem__, -8)

    def test_getslice(self):
        a = bitarray('01001111' '00001')
        self.assertEQUAL(a[:], a)
        self.assertFalse(a[:] is a)
        self.assertEqual(a[13:2:-3], bitarray('1010'))
        self.assertEqual(a[2:-1:4], bitarray('010'))
        self.assertEqual(a[::2], bitarray('0011001'))
        self.assertEqual(a[8:], bitarray('00001'))
        self.assertEqual(a[7:], bitarray('100001'))
        self.assertEqual(a[:8], bitarray('01001111'))
        self.assertEqual(a[::-1], bitarray('10000111' '10010'))
        self.assertEqual(a[:8:-1], bitarray('1000'))

        self.assertRaises(ValueError, a.__getitem__, slice(None, None, 0))
        self.assertRaises(TypeError, a.__getitem__, (1, 2))

    def test_getslice_random(self):
        for a in self.randombitarrays(start=1):
            aa = a.tolist()
            la = len(a)
            for dum in range(10):
                step = self.rndsliceidx(la) or None
                s = slice(self.rndsliceidx(la), self.rndsliceidx(la), step)
                self.assertEQUAL(a[s], bitarray(aa[s], endian=a.endian()))

    def test_setitem_simple(self):
        a = bitarray('0')
        a[0] = 1
        self.assertEqual(a, bitarray('1'))

        a = bitarray(2)
        a[0] = 0
        a[1] = 1
        self.assertEqual(a, bitarray('01'))
        a[-1] = 0
        a[-2] = 1
        self.assertEqual(a, bitarray('10'))

        self.assertRaises(IndexError, a.__setitem__,  2, True)
        self.assertRaises(IndexError, a.__setitem__, -3, False)

    def test_setitem_simple2(self):
        a = bitarray('00000')
        a[0] = 1
        a[-2] = 1
        self.assertEqual(a, bitarray('10010'))
        self.assertRaises(IndexError, a.__setitem__, 5, 'foo')
        self.assertRaises(IndexError, a.__setitem__, -6, 'bar')

    def test_setitem_random(self):
        for a in self.randombitarrays(start=1):
            la = len(a)
            i = randint(0, la - 1)
            aa = a.tolist()
            ida = id(a)
            val = bool(randint(0, 1))
            a[i] = val
            aa[i] = val
            self.assertEqual(a.tolist(), aa)
            self.assertEqual(id(a), ida)
            self.check_obj(a)

    def test_setslice_simple(self):
        for a in self.randombitarrays(start=1):
            la = len(a)
            b = bitarray(la)
            b[0:la] = bitarray(a)
            self.assertEqual(a, b)
            self.assertNotEqual(id(a), id(b))

            b = bitarray(la)
            b[:] = bitarray(a)
            self.assertEqual(a, b)
            self.assertNotEqual(id(a), id(b))

            b = bitarray(la)
            b[::-1] = bitarray(a)
            self.assertEqual(a.tolist()[::-1], b.tolist())

    def test_setslice_random(self):
        for a in self.randombitarrays(start=1):
            la = len(a)
            for dum in range(10):
                step = self.rndsliceidx(la) or None
                s = slice(self.rndsliceidx(la), self.rndsliceidx(la), step)
                lb = randint(0, 10) if step is None else self.slicelen(s, la)
                b = bitarray(lb)
                c = bitarray(a)
                c[s] = b
                self.check_obj(c)
                cc = a.tolist()
                cc[s] = b.tolist()
                self.assertEqual(c, bitarray(cc))

    def test_setslice_self_random(self):
        for a in self.randombitarrays():
            for step in -1, 1:
                s = slice(None, None, step)
                aa = a.tolist()
                a[s] = a
                aa[s] = aa
                self.assertEqual(a, bitarray(aa))

    def test_setslice_self(self):
        a = bitarray('1100111')
        a[::-1] = a
        self.assertEqual(a, bitarray('1110011'))
        a[4:] = a
        self.assertEqual(a, bitarray('11101110011'))
        a[:-5] = a
        self.assertEqual(a, bitarray('1110111001110011'))

        a = bitarray('01001')
        a[:-1] = a
        self.assertEqual(a, bitarray('010011'))
        a[2::] = a
        self.assertEqual(a, bitarray('01010011'))
        a[2:-2:1] = a
        self.assertEqual(a, bitarray('010101001111'))

        a = bitarray('011')
        a[2:2] = a
        self.assertEqual(a, bitarray('010111'))
        a[:] = a
        self.assertEqual(a, bitarray('010111'))

    def test_setslice_to_bitarray(self):
        a = bitarray('11111111' '1111')
        a[2:6] = bitarray('0010')
        self.assertEqual(a, bitarray('11001011' '1111'))
        a.setall(0)
        a[::2] = bitarray('111001')
        self.assertEqual(a, bitarray('10101000' '0010'))
        a.setall(0)
        a[3:] = bitarray('111')
        self.assertEqual(a, bitarray('000111'))

        a = bitarray(12)
        a.setall(0)
        a[1:11:2] = bitarray('11101')
        self.assertEqual(a, bitarray('01010100' '0100'))

        a = bitarray(12)
        a.setall(0)
        a[:-6:-1] = bitarray('10111')
        self.assertEqual(a, bitarray('00000001' '1101'))

    def test_setslice_to_bitarray_2(self):
        a = bitarray('1111')
        a[3:3] = bitarray('000')  # insert
        self.assertEqual(a, bitarray('1110001'))
        a[2:5] = bitarray()  # remove
        self.assertEqual(a, bitarray('1101'))

        a = bitarray('1111')
        a[1:3] = bitarray('0000')
        self.assertEqual(a, bitarray('100001'))
        a[:] = bitarray('010')  # replace all values
        self.assertEqual(a, bitarray('010'))

        # assign slice to bitarray with different length
        a = bitarray('111111')
        a[3:4] = bitarray('00')
        self.assertEqual(a, bitarray('1110011'))
        a[2:5] = bitarray('0')  # remove
        self.assertEqual(a, bitarray('11011'))

    def test_setslice_to_bool(self):
        a = bitarray('11111111')
        a[::2] = False
        self.assertEqual(a, bitarray('01010101'))
        a[4::] = True #                   ^^^^
        self.assertEqual(a, bitarray('01011111'))
        a[-2:] = False #                    ^^
        self.assertEqual(a, bitarray('01011100'))
        a[:2:] = True #               ^^
        self.assertEqual(a, bitarray('11011100'))
        a[:] = True #                 ^^^^^^^^
        self.assertEqual(a, bitarray('11111111'))
        a[2:5] = False #                ^^^
        self.assertEqual(a, bitarray('11000111'))
        a[1::3] = False #              ^  ^  ^
        self.assertEqual(a, bitarray('10000110'))
        a[1:6:2] = True #              ^ ^ ^
        self.assertEqual(a, bitarray('11010110'))
        a[3:3] = False # zero slicelength
        self.assertEqual(a, bitarray('11010110'))

    def test_setslice_to_int(self):
        a = bitarray('11111111')
        a[::2] = 0 #  ^ ^ ^ ^
        self.assertEqual(a, bitarray('01010101'))
        a[4::] = 1 #                      ^^^^
        self.assertEqual(a, bitarray('01011111'))
        a.__setitem__(slice(-2, None, None), 0)
        self.assertEqual(a, bitarray('01011100'))
        self.assertRaises(ValueError, a.__setitem__, slice(None, None, 2), 3)
        self.assertRaises(ValueError, a.__setitem__, slice(None, 2, None), -1)

    def test_setslice_to_invalid(self):
        a = bitarray('11111111')
        s = slice(2, 6, None)
        self.assertRaises(IndexError, a.__setitem__, s, 1.2)
        self.assertRaises(IndexError, a.__setitem__, s, None)
        self.assertRaises(IndexError, a.__setitem__, s, "0110")
        a[s] = False
        self.assertEqual(a, bitarray('11000011'))
        # step != 1 and slicelen != length of assigned bitarray
        self.assertRaisesMessage(
            ValueError,
            "attempt to assign sequence of size 3 to extended slice of size 4",
            a.__setitem__, slice(None, None, 2), bitarray('000'))
        self.assertRaisesMessage(
            ValueError,
            "attempt to assign sequence of size 3 to extended slice of size 2",
            a.__setitem__, slice(None, None, 4), bitarray('000'))
        self.assertRaisesMessage(
            ValueError,
            "attempt to assign sequence of size 7 to extended slice of size 8",
            a.__setitem__, slice(None, None, -1), bitarray('0001000'))
        self.assertEqual(a, bitarray('11000011'))

    def test_sieve(self):  # Sieve of Eratosthenes
        a = bitarray(50)
        a.setall(1)
        for i in range(2, 8):
            if a[i]:
                a[i*i::i] = 0
        primes = [i for i in range(2, 50) if a[i]]
        self.assertEqual(primes, [2, 3, 5, 7, 11, 13, 17, 19,
                                  23, 29, 31, 37, 41, 43, 47])

    def test_delitem(self):
        a = bitarray('100110')
        del a[1]
        self.assertEqual(len(a), 5)
        del a[3], a[-2]
        self.assertEqual(a, bitarray('100'))
        self.assertRaises(IndexError, a.__delitem__,  3)
        self.assertRaises(IndexError, a.__delitem__, -4)

    def test_delslice(self):
        a = bitarray('10101100' '10110')
        del a[3:9] #     ^^^^^   ^
        self.assertEqual(a, bitarray('1010110'))
        del a[::3] #                  ^  ^  ^
        self.assertEqual(a, bitarray('0111'))
        a = bitarray('10101100' '101101111')
        del a[5:-3:3] #    ^     ^  ^
        self.assertEqual(a, bitarray('1010100' '0101111'))
        a = bitarray('10101100' '1011011')
        del a[:-9:-2] #          ^ ^ ^ ^
        self.assertEqual(a, bitarray('10101100' '011'))
        del a[3:3] # zero slicelength
        self.assertEqual(a, bitarray('10101100' '011'))
        self.assertRaises(ValueError, a.__delitem__, slice(None, None, 0))
        self.assertEqual(len(a), 11)
        del a[:]
        self.assertEqual(a, bitarray())

    def test_delslice_random(self):
        for a in self.randombitarrays():
            la = len(a)
            for dum in range(10):
                step = self.rndsliceidx(la) or None
                s = slice(self.rndsliceidx(la), self.rndsliceidx(la), step)
                c = a.copy()
                del c[s]
                self.check_obj(c)
                c_lst = a.tolist()
                del c_lst[s]
                self.assertEQUAL(c, bitarray(c_lst, endian=c.endian()))


tests.append(SliceTests)

# ---------------------------------------------------------------------------

class MiscTests(unittest.TestCase, Util):

    def test_instancecheck(self):
        a = bitarray('011')
        self.assertIsInstance(a, bitarray)
        self.assertFalse(isinstance(a, str))

    def test_booleanness(self):
        self.assertEqual(bool(bitarray('')), False)
        self.assertEqual(bool(bitarray('0')), True)
        self.assertEqual(bool(bitarray('1')), True)

    def test_to01(self):
        a = bitarray()
        self.assertEqual(a.to01(), '')
        self.assertIsInstance(a.to01(), str)

        a = bitarray('101')
        self.assertEqual(a.to01(), '101')
        self.assertIsInstance(a.to01(), str)

    def test_iterate(self):
        for lst in self.randomlists():
            acc = []
            for b in bitarray(lst):
                acc.append(b)
            self.assertEqual(acc, lst)

    def test_iter1(self):
        it = iter(bitarray('011'))
        self.assertEqual(next(it), False)
        self.assertEqual(next(it), True)
        self.assertEqual(next(it), True)
        self.assertStopIteration(it)

    def test_iter2(self):
        for a in self.randombitarrays():
            aa = a.tolist()
            self.assertEqual(list(a), aa)
            self.assertEqual(list(iter(a)), aa)

    def test_assignment(self):
        a = bitarray('00110111001')
        a[1:3] = a[7:9]
        a[-1:] = a[:1]
        b = bitarray('01010111000')
        self.assertEqual(a, b)

    def test_compare_eq_ne(self):
        self.assertTrue(bitarray(0, 'big') == bitarray(0, 'little'))
        self.assertFalse(bitarray(0, 'big') != bitarray(0, 'little'))

        for n in range(1, 20):
            a = bitarray(n, 'little')
            a.setall(1)
            for endian in 'little', 'big':
                b = bitarray(n, endian)
                b.setall(1)
                self.assertTrue(a == b)
                self.assertFalse(a != b)
                b[n - 1] = not b[n - 1]  # flip last bit
                self.assertTrue(a != b)
                self.assertFalse(a == b)

    def test_compare_random(self):
        for a in self.randombitarrays():
            aa = a.tolist()
            for b in self.randombitarrays():
                bb = b.tolist()
                self.assertEqual(a == b, aa == bb)
                self.assertEqual(a != b, aa != bb)
                self.assertEqual(a <= b, aa <= bb)
                self.assertEqual(a <  b, aa <  bb)
                self.assertEqual(a >= b, aa >= bb)
                self.assertEqual(a >  b, aa >  bb)

    def test_subclassing(self):
        class ExaggeratingBitarray(bitarray):

            def __new__(cls, data, offset):
                return bitarray.__new__(cls, data)

            def __init__(self, data, offset):
                self.offset = offset

            def __getitem__(self, i):
                return bitarray.__getitem__(self, i - self.offset)

        for a in self.randombitarrays(start=0):
            b = ExaggeratingBitarray(a, 1234)
            for i in range(len(a)):
                self.assertEqual(a[i], b[i + 1234])

    def test_endianness1(self):
        a = bitarray(endian='little')
        a.frombytes(b'\x01')
        self.assertEqual(a.to01(), '10000000')

        b = bitarray(endian='little')
        b.frombytes(b'\x80')
        self.assertEqual(b.to01(), '00000001')

        c = bitarray(endian='big')
        c.frombytes(b'\x80')
        self.assertEqual(c.to01(), '10000000')

        d = bitarray(endian='big')
        d.frombytes(b'\x01')
        self.assertEqual(d.to01(), '00000001')

        self.assertEqual(a, c)
        self.assertEqual(b, d)

    def test_endianness2(self):
        a = bitarray(8, endian='little')
        a.setall(False)
        a[0] = True
        self.assertEqual(a.tobytes(), b'\x01')
        a[1] = True
        self.assertEqual(a.tobytes(), b'\x03')
        a.frombytes(b' ')
        self.assertEqual(a.tobytes(), b'\x03 ')
        self.assertEqual(a.to01(), '1100000000000100')

    def test_endianness3(self):
        a = bitarray(8, endian='big')
        a.setall(False)
        a[7] = True
        self.assertEqual(a.tobytes(), b'\x01')
        a[6] = True
        self.assertEqual(a.tobytes(), b'\x03')
        a.frombytes(b' ')
        self.assertEqual(a.tobytes(), b'\x03 ')
        self.assertEqual(a.to01(), '0000001100100000')

    def test_endianness4(self):
        a = bitarray('00100000', endian='big')
        self.assertEqual(a.tobytes(), b' ')
        b = bitarray('00000100', endian='little')
        self.assertEqual(b.tobytes(), b' ')
        self.assertNotEqual(a, b)

    def test_endianness5(self):
        a = bitarray('11100000', endian='little')
        b = bitarray(a, endian='big')
        self.assertNotEqual(a, b)
        self.assertEqual(a.tobytes(), b.tobytes())

    def test_pickle(self):
        for a in self.randombitarrays():
            b = pickle.loads(pickle.dumps(a))
            self.assertFalse(b is a)
            self.assertEQUAL(a, b)

    def test_overflow(self):
        if _sysinfo()[0] == 8:
            return

        a = bitarray(2**31 - 1);
        self.assertRaises(OverflowError, bitarray.append, a, True)
        self.assertRaises(IndexError, bitarray.__new__, bitarray, 2**31)

        a = bitarray(10 ** 6)
        self.assertRaises(OverflowError, a.__imul__, 17180)

    def test_unicode_create(self):
        a = bitarray(unicode())
        self.assertEqual(a, bitarray())

        a = bitarray(unicode('111001'))
        self.assertEqual(a, bitarray('111001'))

        for a in self.randombitarrays():
            b = bitarray(unicode(a.to01()))
            self.assertEqual(a, b)

    def test_unicode_extend(self):
        a = bitarray()
        a.extend(unicode())
        self.assertEqual(a, bitarray())

        a = bitarray()
        a.extend(unicode('001011'))
        self.assertEqual(a, bitarray('001011'))

        for a in self.randombitarrays():
            b = bitarray()
            b.extend(unicode(a.to01()))
            self.assertEqual(a, b)

    def test_unhashable(self):
        a = bitarray()
        self.assertRaises(TypeError, hash, a)
        self.assertRaises(TypeError, dict, [(a, 'foo')])

tests.append(MiscTests)

# ---------------------------------------------------------------------------

class SpecialMethodTests(unittest.TestCase, Util):

    def test_all(self):
        a = bitarray()
        self.assertTrue(a.all())
        for s, r in ('0', False), ('1', True), ('01', False):
            self.assertEqual(bitarray(s).all(), r)

        for a in self.randombitarrays():
            self.assertEqual(all(a), a.all())
            self.assertEqual(all(a.tolist()), a.all())

    def test_any(self):
        a = bitarray()
        self.assertFalse(a.any())
        for s, r in ('0', False), ('1', True), ('01', True):
            self.assertEqual(bitarray(s).any(), r)

        for a in self.randombitarrays():
            self.assertEqual(any(a), a.any())
            self.assertEqual(any(a.tolist()), a.any())

    def test_repr(self):
        r = repr(bitarray())
        self.assertEqual(r, "bitarray()")
        self.assertIsInstance(r, str)

        r = repr(bitarray('10111'))
        self.assertEqual(r, "bitarray('10111')")
        self.assertIsInstance(r, str)

        for a in self.randombitarrays():
            b = eval(repr(a))
            self.assertFalse(b is a)
            self.assertEqual(a, b)
            self.check_obj(b)

    def test_copy(self):
        for a in self.randombitarrays():
            b = a.copy()
            self.assertFalse(b is a)
            self.assertEQUAL(a, b)

            b = copy.copy(a)
            self.assertFalse(b is a)
            self.assertEQUAL(a, b)

            b = copy.deepcopy(a)
            self.assertFalse(b is a)
            self.assertEQUAL(a, b)

    def assertReallyEqual(self, a, b):
        # assertEqual first, because it will have a good message if the
        # assertion fails.
        self.assertEqual(a, b)
        self.assertEqual(b, a)
        self.assertTrue(a == b)
        self.assertTrue(b == a)
        self.assertFalse(a != b)
        self.assertFalse(b != a)
        if not is_py3k:
            self.assertEqual(0, cmp(a, b))
            self.assertEqual(0, cmp(b, a))

    def assertReallyNotEqual(self, a, b):
        # assertNotEqual first, because it will have a good message if the
        # assertion fails.
        self.assertNotEqual(a, b)
        self.assertNotEqual(b, a)
        self.assertFalse(a == b)
        self.assertFalse(b == a)
        self.assertTrue(a != b)
        self.assertTrue(b != a)
        if not is_py3k:
            self.assertNotEqual(0, cmp(a, b))
            self.assertNotEqual(0, cmp(b, a))

    def test_equality(self):
        self.assertReallyEqual(bitarray(''), bitarray(''))
        self.assertReallyEqual(bitarray('0'), bitarray('0'))
        self.assertReallyEqual(bitarray('1'), bitarray('1'))

    def test_not_equality(self):
        self.assertReallyNotEqual(bitarray(''), bitarray('1'))
        self.assertReallyNotEqual(bitarray(''), bitarray('0'))
        self.assertReallyNotEqual(bitarray('0'), bitarray('1'))

    def test_equality_random(self):
        for a in self.randombitarrays(start=1):
            b = a.copy()
            self.assertReallyEqual(a, b)
            n = len(a)
            b.invert(n - 1)  # flip last bit
            self.assertReallyNotEqual(a, b)

    def test_sizeof(self):
        a = bitarray()
        size = sys.getsizeof(a)
        self.assertEqual(size, a.__sizeof__())
        self.assertIsInstance(size, int if is_py3k else (int, long))
        self.assertTrue(size < 200)
        a = bitarray(8000)
        self.assertTrue(sys.getsizeof(a) > 1000)


tests.append(SpecialMethodTests)

# ---------------------------------------------------------------------------

class SequenceMethodsTests(unittest.TestCase, Util):

    def test_concat(self):
        c = bitarray('001') + bitarray('110')
        self.assertEQUAL(c, bitarray('001110'))

        for a in self.randombitarrays():
            aa = a.copy()
            for b in self.randombitarrays():
                bb = b.copy()
                c = a + b
                self.assertEqual(c, bitarray(a.tolist() + b.tolist()))
                self.assertEqual(c.endian(), a.endian())
                self.check_obj(c)

                self.assertEQUAL(a, aa)
                self.assertEQUAL(b, bb)

        a = bitarray()
        self.assertRaises(TypeError, a.__add__, 42)

    def test_inplace_concat(self):
        c = bitarray('001')
        c += bitarray('110')
        self.assertEqual(c, bitarray('001110'))
        c += '111'
        self.assertEqual(c, bitarray('001110111'))

        for a in self.randombitarrays():
            for b in self.randombitarrays():
                c = bitarray(a)
                d = c
                d += b
                self.assertEqual(d, a + b)
                self.assertTrue(c is d)
                self.assertEQUAL(c, d)
                self.assertEqual(d.endian(), a.endian())
                self.check_obj(d)

        a = bitarray()
        self.assertRaises(TypeError, a.__iadd__, 42)

    def test_repeat(self):
        for c in [0 * bitarray(),
                  0 * bitarray('1001111'),
                  -1 * bitarray('100110'),
                  11 * bitarray()]:
            self.assertEQUAL(c, bitarray())

        c = 3 * bitarray('001')
        self.assertEQUAL(c, bitarray('001001001'))

        c = bitarray('110') * 3
        self.assertEQUAL(c, bitarray('110110110'))

        for a in self.randombitarrays():
            b = a.copy()
            for n in range(-3, 5):
                c = a * n
                self.assertEQUAL(c, bitarray(n * a.tolist(),
                                             endian=a.endian()))
                c = n * a
                self.assertEqual(c, bitarray(n * a.tolist(),
                                             endian=a.endian()))
                self.assertEQUAL(a, b)

        a = bitarray()
        self.assertRaises(TypeError, a.__mul__, None)

    def test_inplace_repeat(self):
        c = bitarray('1101110011')
        idc = id(c)
        c *= 0
        self.assertEQUAL(c, bitarray())
        self.assertEqual(idc, id(c))

        c = bitarray('110')
        c *= 3
        self.assertEQUAL(c, bitarray('110110110'))

        for a in self.randombitarrays():
            for n in range(-3, 5):
                b = a.copy()
                idb = id(b)
                b *= n
                self.assertEQUAL(b, bitarray(n * a.tolist(),
                                             endian=a.endian()))
                self.assertEqual(idb, id(b))

        a = bitarray()
        self.assertRaises(TypeError, a.__imul__, None)

    def test_contains_simple(self):
        a = bitarray()
        self.assertFalse(False in a)
        self.assertFalse(True in a)
        self.assertTrue(bitarray() in a)
        a.append(True)
        self.assertTrue(True in a)
        self.assertFalse(False in a)
        a = bitarray([False])
        self.assertTrue(False in a)
        self.assertFalse(True in a)
        a.append(True)
        self.assertTrue(0 in a)
        self.assertTrue(1 in a)
        if not is_py3k:
            self.assertTrue(long(0) in a)
            self.assertTrue(long(1) in a)

    def test_contains_errors(self):
        a = bitarray()
        self.assertEqual(a.__contains__(1), False)
        a.append(1)
        self.assertEqual(a.__contains__(1), True)
        a = bitarray('0011')
        self.assertEqual(a.__contains__(bitarray('01')), True)
        self.assertEqual(a.__contains__(bitarray('10')), False)
        self.assertRaises(TypeError, a.__contains__, 'asdf')
        self.assertRaises(ValueError, a.__contains__, 2)
        self.assertRaises(ValueError, a.__contains__, -1)
        if not is_py3k:
            self.assertRaises(ValueError, a.__contains__, long(2))

    def test_contains_range(self):
        for n in range(2, 50):
            a = bitarray(n)
            a.setall(0)
            self.assertTrue(False in a)
            self.assertFalse(True in a)
            a[randint(0, n - 1)] = 1
            self.assertTrue(True in a)
            self.assertTrue(False in a)
            a.setall(1)
            self.assertTrue(True in a)
            self.assertFalse(False in a)
            a[randint(0, n - 1)] = 0
            self.assertTrue(True in a)
            self.assertTrue(False in a)

    def test_contains_explicit(self):
        a = bitarray('011010000001')
        for s, r in [('', True), ('1', True), ('11', True), ('111', False),
                     ('011', True), ('0001', True), ('00011', False)]:
            self.assertEqual(bitarray(s) in a, r)


tests.append(SequenceMethodsTests)

# ---------------------------------------------------------------------------

class NumberMethodsTests(unittest.TestCase, Util):

    def test_misc(self):
        for a in self.randombitarrays():
            b = ~a
            c = a & b
            self.assertEqual(c.any(), False)
            self.assertEqual(a, a ^ c)
            d = a ^ b
            self.assertEqual(d.all(), True)
            b &= d
            self.assertEqual(~b, a)

    def test_size_and_endianness(self):
        a = bitarray('11001')
        b = bitarray('100111')
        self.assertRaises(ValueError, a.__and__, b)
        for x in a.__or__, a.__xor__, a.__iand__, a.__ior__, a.__ixor__:
            self.assertRaises(ValueError, x, b)
        a = bitarray('11001', 'big')
        b = bitarray('10011', 'little')
        self.assertRaises(ValueError, a.__and__, b)
        for x in a.__or__, a.__xor__, a.__iand__, a.__ior__, a.__ixor__:
            self.assertRaises(ValueError, x, b)

    def test_and(self):
        a = bitarray('11001')
        b = bitarray('10011')
        self.assertEQUAL(a & b, bitarray('10001'))

        b = bitarray('1001')
        self.assertRaises(ValueError, a.__and__, b)  # not same length
        if is_py3k:  # XXX: note sure why this is failing on Py27
            self.assertRaises(TypeError, a.__and__, 42)

    def test_iand(self):
        a =  bitarray('110010110')
        ida = id(a)
        a &= bitarray('100110011')
        self.assertEQUAL(a, bitarray('100010010'))
        self.assertEqual(ida, id(a))

    def test_or(self):
        a = bitarray('11001')
        b = bitarray('10011')
        aa = a.copy()
        bb = b.copy()
        self.assertEQUAL(a | b, bitarray('11011'))
        self.assertEQUAL(a, aa)
        self.assertEQUAL(b, bb)

    def test_ior(self):
        a = bitarray('110010110')
        b = bitarray('100110011')
        bb = b.copy()
        a |= b
        self.assertEQUAL(a, bitarray('110110111'))
        self.assertEQUAL(b, bb)

    def test_xor(self):
        a = bitarray('11001')
        b = bitarray('10011')
        self.assertEQUAL(a ^ b, bitarray('01010'))

    def test_ixor(self):
        a =  bitarray('110010110')
        a ^= bitarray('100110011')
        self.assertEQUAL(a, bitarray('010100101'))

    def test_invert(self):
        a = bitarray('11011')
        b = ~a
        self.assertEQUAL(b, bitarray('00100'))
        self.assertEQUAL(a, bitarray('11011'))
        self.assertFalse(a is b)

        for a in self.randombitarrays():
            b = bitarray(a)
            b.invert()
            for i in range(len(a)):
                self.assertEqual(b[i], not a[i])
            self.check_obj(b)
            c = ~a
            self.assertEQUAL(c, b)
            self.check_obj(c)


tests.append(NumberMethodsTests)

# ---------------------------------------------------------------------------

class ExtendTests(unittest.TestCase, Util):

    def test_wrongArgs(self):
        a = bitarray()
        self.assertRaises(TypeError, a.extend)
        self.assertRaises(TypeError, a.extend, None)
        self.assertRaises(TypeError, a.extend, True)
        self.assertRaises(TypeError, a.extend, 24)
        self.assertRaises(ValueError, a.extend, '0011201')

    def test_bitarray(self):
        a = bitarray()
        a.extend(bitarray())
        self.assertEqual(a, bitarray())
        a.extend(bitarray('110'))
        self.assertEqual(a, bitarray('110'))
        a.extend(bitarray('1110'))
        self.assertEqual(a, bitarray('1101110'))

        a = bitarray('00001111', endian='little')
        a.extend(bitarray('00111100', endian='big'))
        self.assertEqual(a, bitarray('0000111100111100'))

        for a in self.randombitarrays():
            for b in self.randombitarrays():
                c = bitarray(a)
                idc = id(c)
                c.extend(b)
                self.assertEqual(id(c), idc)
                self.assertEqual(c, a + b)

    def test_list(self):
        a = bitarray()
        a.extend([0, 1, 3, None, {}])
        self.assertEqual(a, bitarray('01100'))
        a.extend([True, False])
        self.assertEqual(a, bitarray('0110010'))

        for a in self.randomlists():
            for b in self.randomlists():
                c = bitarray(a)
                idc = id(c)
                c.extend(b)
                self.assertEqual(id(c), idc)
                self.assertEqual(c.tolist(), a + b)
                self.check_obj(c)

    def test_tuple(self):
        a = bitarray()
        a.extend((0, 1, 2, 0, 3))
        self.assertEqual(a, bitarray('01101'))

        for a in self.randomlists():
            for b in self.randomlists():
                c = bitarray(a)
                idc = id(c)
                c.extend(tuple(b))
                self.assertEqual(id(c), idc)
                self.assertEqual(c.tolist(), a + b)
                self.check_obj(c)

    def test_generator(self):
        def bar():
            for x in ('', '1', None, True, []):
                yield x
        a = bitarray()
        a.extend(bar())
        self.assertEqual(a, bitarray('01010'))

        for a in self.randomlists():
            for b in self.randomlists():
                def foo():
                    for e in b:
                        yield e
                c = bitarray(a)
                idc = id(c)
                c.extend(foo())
                self.assertEqual(id(c), idc)
                self.assertEqual(c.tolist(), a + b)
                self.check_obj(c)

    def test_iterator1(self):
        a = bitarray()
        a.extend(iter([3, 9, 0, 1, -2]))
        self.assertEqual(a, bitarray('11011'))

        for a in self.randomlists():
            for b in self.randomlists():
                c = bitarray(a)
                idc = id(c)
                c.extend(iter(b))
                self.assertEqual(id(c), idc)
                self.assertEqual(c.tolist(), a + b)
                self.check_obj(c)

    def test_iterator2(self):
        a = bitarray()
        a.extend(itertools.repeat(True, 23))
        self.assertEqual(a, bitarray(23 * '1'))

    def test_string01(self):
        a = bitarray()
        a.extend('0110111')
        self.assertEqual(a, bitarray('0110111'))

        for a in self.randomlists():
            for b in self.randomlists():
                c = bitarray(a)
                idc = id(c)
                c.extend(''.join(['0', '1'][x] for x in b))
                self.assertEqual(id(c), idc)
                self.assertEqual(c.tolist(), a + b)
                self.check_obj(c)

    def test_extend_self(self):
        a = bitarray()
        a.extend(a)
        self.assertEqual(a, bitarray())

        a = bitarray('1')
        a.extend(a)
        self.assertEqual(a, bitarray('11'))

        a = bitarray('110')
        a.extend(a)
        self.assertEqual(a, bitarray('110110'))

        for a in self.randombitarrays():
            b = bitarray(a)
            a.extend(a)
            self.assertEqual(a, b + b)


tests.append(ExtendTests)

# ---------------------------------------------------------------------------

class MethodTests(unittest.TestCase, Util):

    def test_append_simple(self):
        a = bitarray()
        a.append(True)
        a.append(False)
        a.append(False)
        self.assertEQUAL(a, bitarray('100'))
        a.append(0)
        a.append(1)
        a.append(2)
        a.append(None)
        a.append('')
        a.append('a')
        self.assertEQUAL(a, bitarray('100011001'))

    def test_append_random(self):
        for a in self.randombitarrays():
            aa = a.tolist()
            b = a
            b.append(1)
            self.assertTrue(a is b)
            self.check_obj(b)
            self.assertEQUAL(b, bitarray(aa + [1], endian=a.endian()))
            b.append('')
            self.assertEQUAL(b, bitarray(aa + [1, 0], endian=a.endian()))

    def test_insert(self):
        a = bitarray()
        b = a
        a.insert(0, True)
        self.assertTrue(a is b)
        self.assertEqual(a, bitarray('1'))
        self.assertRaises(TypeError, a.insert)
        self.assertRaises(TypeError, a.insert, None)

        for a in self.randombitarrays():
            aa = a.tolist()
            for _ in range(50):
                item = bool(randint(0, 1))
                pos = randint(-len(a) - 2, len(a) + 2)
                a.insert(pos, item)
                aa.insert(pos, item)
                self.assertEqual(a.tolist(), aa)
                self.check_obj(a)

    def test_index1(self):
        a = bitarray()
        for i in (True, False, 1, 0):
            self.assertRaises(ValueError, a.index, i)

        a = bitarray(100 * [False])
        self.assertRaises(ValueError, a.index, True)
        self.assertRaises(TypeError, a.index)
        self.assertRaises(TypeError, a.index, 1, 'a')
        self.assertRaises(TypeError, a.index, 1, 0, 'a')
        self.assertRaises(TypeError, a.index, 1, 0, 100, 1)
        a[20] = a[27] = 1
        self.assertEqual(a.index(42), 20)
        self.assertEqual(a.index(1, 21), 27)
        self.assertEqual(a.index(1, 27), 27)
        self.assertEqual(a.index(1, -73), 27)
        self.assertRaises(ValueError, a.index, 1, 5, 17)
        self.assertRaises(ValueError, a.index, 1, 5, -83)
        self.assertRaises(ValueError, a.index, 1, 21, 27)
        self.assertRaises(ValueError, a.index, 1, 28)
        self.assertEqual(a.index(0), 0)

        a = bitarray(200 * [True])
        self.assertRaises(ValueError, a.index, False)
        a[173] = a[187] = 0
        self.assertEqual(a.index(False), 173)
        self.assertEqual(a.index(True), 0)

    def test_index2(self):
        for n in range(50):
            for m in range(n):
                a = bitarray(n)
                a.setall(0)
                self.assertRaises(ValueError, a.index, 1)
                a[m] = 1
                self.assertEqual(a.index(1), m)

                a.setall(1)
                self.assertRaises(ValueError, a.index, 0)
                a[m] = 0
                self.assertEqual(a.index(0), m)

    def test_index3(self):
        a = bitarray('00001000' '00000000' '0010000')
        self.assertEqual(a.index(1), 4)
        self.assertEqual(a.index(1, 1), 4)
        self.assertEqual(a.index(0, 4), 5)
        self.assertEqual(a.index(1, 5), 18)
        self.assertRaises(ValueError, a.index, 1, 5, 18)
        self.assertRaises(ValueError, a.index, 1, 19)

    def test_index4(self):
        a = bitarray('11110111' '11111111' '1101111')
        self.assertEqual(a.index(0), 4)
        self.assertEqual(a.index(0, 1), 4)
        self.assertEqual(a.index(1, 4), 5)
        self.assertEqual(a.index(0, 5), 18)
        self.assertRaises(ValueError, a.index, 0, 5, 18)
        self.assertRaises(ValueError, a.index, 0, 19)

    def test_index5(self):
        a = bitarray(2000)
        a.setall(0)
        for _ in range(3):
            a[randint(0, 1999)] = 1
        aa = a.tolist()
        for _ in range(100):
            start = randint(0, 2000)
            stop = randint(0, 2000)
            try:
                res1 = a.index(1, start, stop)
            except ValueError:
                res1 = None
            try:
                res2 = aa.index(1, start, stop)
            except ValueError:
                res2 = None
            self.assertEqual(res1, res2)

    def test_index6(self):
        for n in range(1, 50):
            a = bitarray(n)
            i = randint(0, 1)
            a.setall(i)
            for unused in range(randint(1, 4)):
                a[randint(0, n - 1)] = 1 - i
            aa = a.tolist()
            for unused in range(100):
                start = randint(-50, n + 50)
                stop = randint(-50, n + 50)
                try:
                    res1 = a.index(1 - i, start, stop)
                except ValueError:
                    res1 = None
                try:
                    res2 = aa.index(1 - i, start, stop)
                except ValueError:
                    res2 = None
                self.assertEqual(res1, res2)


    def test_count_basic(self):
        a = bitarray('10011')
        self.assertEqual(a.count(), 3)
        self.assertEqual(a.count(True), 3)
        self.assertEqual(a.count(False), 2)
        self.assertEqual(a.count(1), 3)
        self.assertEqual(a.count(0), 2)
        self.assertEqual(a.count(None), 2)
        self.assertEqual(a.count(''), 2)
        self.assertEqual(a.count('A'), 3)
        self.assertRaises(TypeError, a.count, 0, 'A')
        self.assertRaises(TypeError, a.count, 0, 0, 'A')

    def test_count_byte(self):

        def count(n):  # count 1 bits in number
            cnt = 0
            while n:
                cnt += n & 1
                n >>= 1
            return cnt

        for i in range(256):
            a = bitarray()
            a.frombytes(bytes(bytearray([i])))
            self.assertEqual(len(a), 8)
            self.assertEqual(a.count(), count(i))
            self.assertEqual(a.count(), bin(i)[2:].count('1'))

    def test_count_whole_range(self):
        for a in self.randombitarrays():
            s = a.to01()
            self.assertEqual(a.count(1), s.count('1'))
            self.assertEqual(a.count(0), s.count('0'))

    def test_count_allones(self):
        N = 37
        a = bitarray(N)
        a.setall(1)
        for i in range(N):
            for j in range(i, N):
                self.assertEqual(a.count(1, i, j), j - i)

    def test_count_explicit(self):
        for endian in 'big', 'little':
            a = bitarray('01001100' '01110011' '01', endian)
            self.assertEqual(a.count(), 9)
            self.assertEqual(a.count(0, 12), 3)
            self.assertEqual(a.count(1, -5), 3)
            self.assertEqual(a.count(1, 2, 17), 7)
            self.assertEqual(a.count(1, 6, 11), 2)
            self.assertEqual(a.count(0, 7, -3), 4)
            self.assertEqual(a.count(1, 1, -1), 8)
            self.assertEqual(a.count(1, 17, 14), 0)

    def test_count_random(self):
        for a in self.randombitarrays():
            s = a.to01()
            i = randint(-3, len(a) + 1)
            j = randint(-3, len(a) + 1)
            self.assertEqual(a.count(1, i, j), s[i:j].count('1'))
            self.assertEqual(a.count(0, i, j), s[i:j].count('0'))

    def test_search(self):
        a = bitarray('')
        self.assertEqual(a.search(bitarray('0')), [])
        self.assertEqual(a.search(bitarray('1')), [])

        a = bitarray('1')
        self.assertEqual(a.search(bitarray('0')), [])
        self.assertEqual(a.search(bitarray('1')), [0])
        self.assertEqual(a.search(bitarray('11')), [])

        a = bitarray(100*'1')
        self.assertEqual(a.search(bitarray('0')), [])
        self.assertEqual(a.search(bitarray('1')), list(range(100)))

        a = bitarray('10010101110011111001011')
        for limit in range(10):
            self.assertEqual(a.search(bitarray('011'), limit),
                             [6, 11, 20][:limit])
        self.assertRaises(ValueError, a.search, bitarray())
        self.assertRaises(TypeError, a.search, '010')

    def test_itersearch(self):
        a = bitarray('10011')
        self.assertRaises(ValueError, a.itersearch, bitarray())
        self.assertRaises(TypeError, a.itersearch, '')
        it = a.itersearch(bitarray('1'))
        self.assertEqual(next(it), 0)
        self.assertEqual(next(it), 3)
        self.assertEqual(next(it), 4)
        self.assertStopIteration(it)

    def test_search2(self):
        a = bitarray('10011')
        for s, res in [('0',     [1, 2]),  ('1', [0, 3, 4]),
                       ('01',    [2]),     ('11', [3]),
                       ('000',   []),      ('1001', [0]),
                       ('011',   [2]),     ('0011', [1]),
                       ('10011', [0]),     ('100111', [])]:
            b = bitarray(s)
            self.assertEqual(a.search(b), res)
            self.assertEqual([p for p in a.itersearch(b)], res)

    def test_search3(self):
        a = bitarray('10010101110011111001011')
        for s, res in [('011', [6, 11, 20]),
                       ('111', [7, 12, 13, 14]),  # note the overlap
                       ('1011', [5, 19]),
                       ('100', [0, 9, 16])]:
            b = bitarray(s)
            self.assertEqual(a.search(b), res)
            self.assertEqual(list(a.itersearch(b)), res)
            self.assertEqual([p for p in a.itersearch(b)], res)

    def test_search4(self):
        for a in self.randombitarrays():
            aa = a.to01()
            for sub in '0', '1', '01', '01', '11', '101', '1111111':
                sr = a.search(bitarray(sub), 1)
                try:
                    p = sr[0]
                except IndexError:
                    p = -1
                self.assertEqual(p, aa.find(sub))

    def test_search_type(self):
        a = bitarray('10011')
        it = a.itersearch(bitarray('1'))
        self.assertIsInstance(type(it), type)

    def test_fill_simple(self):
        for endian in 'little', 'big':
            a = bitarray(endian=endian)
            self.assertEqual(a.fill(), 0)
            self.assertEqual(len(a), 0)

            a = bitarray('101', endian)
            self.assertEqual(a.fill(), 5)
            self.assertEqual(a, bitarray('10100000'))
            self.assertEqual(a.fill(), 0)
            self.assertEqual(a, bitarray('10100000'))

    def test_fill_random(self):
        for a in self.randombitarrays():
            b = a.copy()
            res = b.fill()
            self.assertTrue(0 <= res < 8)
            self.assertEqual(b.endian(), a.endian())
            self.check_obj(b)
            if len(a) % 8 == 0:
                self.assertEqual(b, a)
            else:
                self.assertTrue(len(b) % 8 == 0)
                self.assertNotEqual(b, a)
                self.assertEqual(b[:len(a)], a)
                self.assertEqual(b[len(a):],
                                 (len(b) - len(a)) * bitarray('0'))

    def test_invert_simple(self):
        a = bitarray()
        a.invert()
        self.assertEQUAL(a, bitarray())

        a = bitarray('11011')
        a.invert()
        self.assertEQUAL(a, bitarray('00100'))
        a.invert(2)
        self.assertEQUAL(a, bitarray('00000'))
        a.invert(-1)
        self.assertEQUAL(a, bitarray('00001'))

    def test_invert_errors(self):
        a = bitarray(5)
        self.assertRaises(IndexError, a.invert, 5)
        self.assertRaises(IndexError, a.invert, -6)
        self.assertRaises(TypeError, a.invert, "A")
        self.assertRaises(TypeError, a.invert, 0, 1)

    def test_invert_random(self):
        for a in self.randombitarrays(start=1):
            b = a.copy()
            c = a.copy()
            i = randint(0, len(a) - 1)
            b.invert(i)
            c[i] = not c[i]
            self.assertEQUAL(b, c)

    def test_sort_simple(self):
        a = bitarray('1101000')
        a.sort()
        self.assertEqual(a, bitarray('0000111'))

        a = bitarray('1101000')
        a.sort(reverse=True)
        self.assertEqual(a, bitarray('1110000'))
        a.sort(reverse=False)
        self.assertEqual(a, bitarray('0000111'))
        a.sort(True)
        self.assertEqual(a, bitarray('1110000'))
        a.sort(False)
        self.assertEqual(a, bitarray('0000111'))

        self.assertRaises(TypeError, a.sort, 'A')

    def test_sort_random(self):
        for rev in 0, 1:
            for a in self.randombitarrays():
                b = a.tolist()
                a.sort(rev)
                self.assertEqual(a, bitarray(sorted(b, reverse=rev)))

    def test_reverse_simple(self):
        for x, y in [('', ''), ('1', '1'), ('10', '01'), ('001', '100'),
                     ('1110', '0111'), ('11100', '00111'),
                     ('011000', '000110'), ('1101100', '0011011'),
                     ('11110000', '00001111'),
                     ('11111000011', '11000011111'),
                     ('11011111' '00100000' '000111',
                      '111000' '00000100' '11111011')]:
            a = bitarray(x)
            a.reverse()
            self.assertEQUAL(a, bitarray(y))

        self.assertRaises(TypeError, bitarray().reverse, 42)

    def test_reverse_random(self):
        for a in self.randombitarrays():
            b = a.copy()
            a.reverse()
            self.assertEQUAL(a, bitarray(b.tolist()[::-1], endian=a.endian()))
            self.assertEQUAL(a, b[::-1])

    def test_tolist(self):
        a = bitarray()
        self.assertEqual(a.tolist(), [])

        a = bitarray('110')
        self.assertEqual(a.tolist(), [True, True, False])
        self.assertEqual(a.tolist(True), [1, 1, 0])

        for as_ints in 0, 1:
            for elt in a.tolist(as_ints):
                self.assertIsInstance(elt, int if as_ints else bool)

        for lst in self.randomlists():
            a = bitarray(lst)
            self.assertEqual(a.tolist(), lst)

    def test_remove(self):
        a = bitarray('1010110')
        for val, res in [(False, '110110'), (True, '10110'),
                         (1, '0110'), (1, '010'), (0, '10'),
                         (0, '1'), (1, '')]:
            a.remove(val)
            self.assertEQUAL(a, bitarray(res))

        a = bitarray('0010011')
        b = a
        b.remove('1')
        self.assertTrue(b is a)
        self.assertEQUAL(b, bitarray('000011'))

    def test_remove_errors(self):
        a = bitarray()
        for i in (True, False, 1, 0):
            self.assertRaises(ValueError, a.remove, i)

        a = bitarray(21)
        a.setall(0)
        self.assertRaises(ValueError, a.remove, 1)
        a.setall(1)
        self.assertRaises(ValueError, a.remove, 0)

    def test_pop_simple(self):
        for x, n, r, y in [('1', 0, True, ''),
                           ('0', -1, False, ''),
                           ('0011100', 3, True, '001100')]:
            a = bitarray(x)
            self.assertEqual(a.pop(n), r)
            self.assertEqual(a, bitarray(y))

        a = bitarray('01')
        self.assertEqual(a.pop(), True)
        self.assertEqual(a.pop(), False)
        self.assertRaises(IndexError, a.pop)

    def test_pop_random(self):
        for a in self.randombitarrays():
            self.assertRaises(IndexError, a.pop, len(a))
            self.assertRaises(IndexError, a.pop, -len(a) - 1)
            if len(a) == 0:
                continue
            aa = a.tolist()
            enda = a.endian()
            self.assertEqual(a.pop(), aa[-1])
            self.check_obj(a)
            self.assertEqual(a.endian(), enda)

        for a in self.randombitarrays(start=1):
            n = randint(-len(a), len(a)-1)
            aa = a.tolist()
            self.assertEqual(a.pop(n), aa[n])
            aa.pop(n)
            self.assertEqual(a, bitarray(aa))
            self.check_obj(a)

    def test_clear(self):
        for a in self.randombitarrays():
            ida = id(a)
            endian = a.endian()
            a.clear()
            self.assertEqual(a, bitarray())
            self.assertEqual(id(a), ida)
            self.assertEqual(a.endian(), endian)
            self.assertEqual(len(a), 0)

    def test_setall(self):
        a = bitarray(5)
        a.setall(True)
        self.assertEQUAL(a, bitarray('11111'))
        a.setall(False)
        self.assertEQUAL(a, bitarray('00000'))

    def test_setall_empty(self):
        a = bitarray()
        for v in 0, 1:
            a.setall(v)
            self.assertEQUAL(a, bitarray())

    def test_setall_random(self):
        for a in self.randombitarrays():
            val = randint(0, 1)
            b = a
            b.setall(val)
            self.assertEqual(b, bitarray(len(b) * [val]))
            self.assertTrue(a is b)
            self.check_obj(b)

    def test_bytereverse_explicit(self):
        for x, y in [('', ''),
                     ('1', '0'),
                     ('1011', '0000'),
                     ('111011', '001101'),
                     ('11101101', '10110111'),
                     ('000000011', '100000000'),
                     ('11011111' '00100000' '000111',
                      '11111011' '00000100' '001110')]:
            a = bitarray(x)
            a.bytereverse()
            self.assertEqual(a, bitarray(y))

    def test_bytereverse_byte(self):
        for i in range(256):
            a = bitarray()
            a.frombytes(bytes(bytearray([i])))
            b = a.copy()
            b.bytereverse()
            self.assertEqual(b, a[::-1])
            self.check_obj(b)

    def test_eval_monic(self):
        input = '11100011' + '11001100'
        a = bitarray(input)
        b = bitarray()

        self.assertEqual(b.eval_monic(a, 2, 16), bitarray('1'))
        self.assertEqual(b.eval_monic(a, 9, 16), bitarray('1'))
        self.assertEqual(b.eval_monic(a, 10, 16), bitarray('0'))

        self.assertEqual(b.eval_monic(a, 0, 8), bitarray('11'))
        self.assertEqual(b.eval_monic(a, 2, 8), bitarray('10'))

        self.assertEqual(b.eval_monic(a*100, 0, 8), bitarray('11'*100))
        self.assertEqual(b.eval_monic(a*100, 2, 8), bitarray('10'*100))
        self.assertEqual(b.eval_monic(a*10, 0, 1), a*10)

    def test_fast_hw_ops(self):
        for a in self.randombitarrays():
            b = bitarray(len(a), endian=a.endian())
            b.frombytes(os.urandom(bits2bytes(len(a))))
            del b[len(a):]

            self.assertEqual((a & b).count(1), a.fast_hw_and(b))
            self.assertEqual((a | b).count(1), a.fast_hw_or(b))
            self.assertEqual((a ^ b).count(1), a.fast_hw_xor(b))

    def test_eval_polynomial01(self):
        from bitarray import tbase
        dat = '11001100 00110011'.replace(' ', '')
        a = bitarray(dat, endian=self.randomendian())
        vars = 8
        base = [bitarray(vars, endian=a.endian()) for _ in range(vars)]
        for x in range(vars):
            base[x].eval_monic(a, x, vars)
        bb = tbase(base)
        bb2 = tbase(base, 32)

        self.assertEqual(base[0], bitarray('10', endian=a.endian()))
        self.assertEqual(base[1], bitarray('10', endian=a.endian()))
        self.assertEqual(base[-1], bitarray('01', endian=a.endian()))

        self.assertEqual(bb.eval_poly_hw([[0]]), 1)
        self.assertEqual(bb.eval_poly_hw([[1]]), 1)
        self.assertEqual(bb.eval_poly_hw([[2]]), 1)
        self.assertEqual(bb.eval_poly_hw([[0, 1]]), 1)
        self.assertEqual(bb.eval_poly_hw([[0, 1], [3]]), 2)
        self.assertEqual(bb.eval_poly_hw([[0, 1], [4, 5]]), 0)
        self.assertEqual(bb.eval_poly_hw([[0, 1], [2, 3]]), 2)
        self.assertEqual(bb.eval_poly_hw(None, [
            [[0, 1], [2, 3]],
            [[0, 1], [3]],
            [[0]],
        ]), [2, 2, 1])
        self.assertEqual(bb2.eval_poly_hw(None, [
            [[0, 1], [2, 3]],
            [[0, 1], [3]],
            [[0]],
        ]), [2, 2, 1])

    def test_poly_base(self):
        from bitarray import tbase
        for vars in range(12, 133):
            a = self.randombitarray(vars * randint(1000, 2000))
            base = [bitarray(vars, endian=a.endian()) for x in range(vars)]
            for x in range(vars):
                base[x].eval_monic(a, x, vars)

            for i in range(1500):
                bb = tbase(base)
            bb2 = tbase(base, 32)

            poly = []
            for t in range(randint(1, 4)):
                b = []
                for x in range(randint(1, 4)):
                    b.append(randint(0, vars - 1))
                poly.append(b)

            res = bitarray(len(base[0]), endian=a.endian())
            res.setall(0)
            for ix, term in enumerate(poly):
                sub = bitarray(base[term[0]])
                for ax, var in enumerate(term[1:]):
                    sub &= base[var]
                res ^= sub

            base = []
            for i in range(8192):
                base.append((i, i))

            c = bb.eval_poly_hw(poly)
            c2 = bb2.eval_poly_hw(poly)
            self.assertEqual(res.count(), c)
            self.assertEqual(res.count(), c2)
            bb = None


tests.append(MethodTests)

# ---------------------------------------------------------------------------

class BytesTests(unittest.TestCase, Util):

    def randombytes(self):
        for n in range(1, 20):
            yield os.urandom(n)

    def test_frombytes_simple(self):
        a = bitarray(endian='big')
        a.frombytes(b'A')
        self.assertEqual(a, bitarray('01000001'))

        b = a
        b.frombytes(b'BC')
        self.assertEQUAL(b, bitarray('01000001' '01000010' '01000011',
                                     endian='big'))
        self.assertTrue(b is a)

    def test_frombytes_empty(self):
        for a in self.randombitarrays():
            b = a.copy()
            a.frombytes(b'')
            self.assertEQUAL(a, b)
            self.assertFalse(a is b)

    def test_frombytes_errors(self):
        a = bitarray()
        self.assertRaises(TypeError, a.frombytes)
        self.assertRaises(TypeError, a.frombytes, b'', b'')
        self.assertRaises(TypeError, a.frombytes, 1)

    def test_frombytes_random(self):
        for b in self.randombitarrays():
            for s in self.randombytes():
                a = bitarray(endian=b.endian())
                a.frombytes(s)
                c = b.copy()
                b.frombytes(s)
                self.assertEQUAL(b[-len(a):], a)
                self.assertEQUAL(b[:-len(a)], c)
                self.assertEQUAL(b, c + a)


    def test_tobytes_empty(self):
        a = bitarray()
        self.assertEqual(a.tobytes(), b'')

    def test_tobytes_endian(self):
        for end in ('big', 'little'):
            a = bitarray(endian=end)
            a.frombytes(b'foo')
            self.assertEqual(a.tobytes(), b'foo')

            for s in self.randombytes():
                a = bitarray(endian=end)
                a.frombytes(s)
                self.assertEqual(a.tobytes(), s)

    def test_tobytes_explicit_ones(self):
        for n, s in [(1, b'\x01'), (2, b'\x03'), (3, b'\x07'), (4, b'\x0f'),
                     (5, b'\x1f'), (6, b'\x3f'), (7, b'\x7f'), (8, b'\xff'),
                     (12, b'\xff\x0f'), (15, b'\xff\x7f'), (16, b'\xff\xff'),
                     (17, b'\xff\xff\x01'), (24, b'\xff\xff\xff')]:
            a = bitarray(n, endian='little')
            a.setall(1)
            self.assertEqual(a.tobytes(), s)


    def test_unpack_simple(self):
        a = bitarray('01')
        self.assertIsInstance(a.unpack(), bytes)
        self.assertEqual(a.unpack(), b'\x00\xff')
        self.assertEqual(a.unpack(b'A'), b'A\xff')
        self.assertEqual(a.unpack(b'0', b'1'), b'01')
        self.assertEqual(a.unpack(one=b'\x01'), b'\x00\x01')
        self.assertEqual(a.unpack(zero=b'A'), b'A\xff')
        self.assertEqual(a.unpack(one=b't', zero=b'f'), b'ft')

    def test_unpack_random(self):
        for a in self.randombitarrays():
            self.assertEqual(a.unpack(b'0', b'1'),
                             a.to01().encode())
            # round trip
            b = bitarray()
            b.pack(a.unpack())
            self.assertEqual(b, a)
            # round trip with invert
            b = bitarray()
            b.pack(a.unpack(b'\x01', b'\x00'))
            b.invert()
            self.assertEqual(b, a)

    def test_unpack_errors(self):
        a = bitarray('01')
        self.assertRaises(TypeError, a.unpack, b'')
        self.assertRaises(TypeError, a.unpack, b'0', b'')
        self.assertRaises(TypeError, a.unpack, b'a', zero=b'b')
        self.assertRaises(TypeError, a.unpack, foo=b'b')
        self.assertRaises(TypeError, a.unpack, one=b'aa', zero=b'b')
        if is_py3k:
            self.assertRaises(TypeError, a.unpack, '0')
            self.assertRaises(TypeError, a.unpack, one='a')
            self.assertRaises(TypeError, a.unpack, b'0', '1')

    def test_pack_simple(self):
        for endian in 'little', 'big':
            _set_default_endian(endian)
            a = bitarray()
            a.pack(b'\x00')
            self.assertEqual(a, bitarray('0'))
            a.pack(b'\xff')
            self.assertEqual(a, bitarray('01'))
            a.pack(b'\x01\x00\x7a')
            self.assertEqual(a, bitarray('01101'))

    def test_pack_random(self):
        a = bitarray()
        for n in range(256):
            a.pack(bytes(bytearray([n])))
        self.assertEqual(a, bitarray('0' + 255 * '1'))

    def test_pack_errors(self):
        a = bitarray()
        self.assertRaises(TypeError, a.pack, 0)
        if is_py3k:
            self.assertRaises(TypeError, a.pack, '1')
        self.assertRaises(TypeError, a.pack, [1, 3])
        self.assertRaises(TypeError, a.pack, bitarray())


tests.append(BytesTests)

# ---------------------------------------------------------------------------

class FileTests(unittest.TestCase, Util):

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.tmpfname = os.path.join(self.tmpdir, 'testfile')

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def read_file(self):
        with open(self.tmpfname, 'rb') as fi:
            return fi.read()

    def assertFileSize(self, size):
        self.assertEqual(os.path.getsize(self.tmpfname), size)


    def test_pickle(self):
        for a in self.randombitarrays():
            with open(self.tmpfname, 'wb') as fo:
                pickle.dump(a, fo)
            with open(self.tmpfname, 'rb') as fi:
                b = pickle.load(fi)
            self.assertFalse(b is a)
            self.assertEQUAL(a, b)

    def test_shelve(self):
        if not shelve or hasattr(sys, 'gettotalrefcount'):
            return

        d = shelve.open(self.tmpfname)
        stored = []
        for a in self.randombitarrays():
            key = hashlib.md5(repr(a).encode() +
                              a.endian().encode()).hexdigest()
            d[key] = a
            stored.append((key, a))
        d.close()
        del d

        d = shelve.open(self.tmpfname)
        for k, v in stored:
            self.assertEQUAL(d[k], v)
        d.close()


    def test_fromfile_empty(self):
        with open(self.tmpfname, 'wb') as fo:
            pass
        self.assertFileSize(0)

        a = bitarray()
        with open(self.tmpfname, 'rb') as fi:
            a.fromfile(fi)
        self.assertEqual(a, bitarray())

    def test_fromfile_Foo(self):
        with open(self.tmpfname, 'wb') as fo:
            fo.write(b'Foo')
        self.assertFileSize(3)

        a = bitarray(endian='big')
        with open(self.tmpfname, 'rb') as fi:
            a.fromfile(fi)
        self.assertEqual(a, bitarray('01000110' '01101111' '01101111'))

        a = bitarray(endian='little')
        with open(self.tmpfname, 'rb') as fi:
            a.fromfile(fi)
        self.assertEqual(a, bitarray('01100010' '11110110' '11110110'))

    def test_fromfile_wrong_args(self):
        a = bitarray()
        self.assertRaises(TypeError, a.fromfile)
        #self.assertRaises(TypeError, a.fromfile, StringIO())  # file not open
        self.assertRaises(Exception, a.fromfile, 42)
        self.assertRaises(Exception, a.fromfile, 'bar')

        with open(self.tmpfname, 'wb') as fo:
            pass
        with open(self.tmpfname, 'rb') as fi:
            self.assertRaises(TypeError, a.fromfile, fi, None)

    def test_fromfile_erros(self):
        with open(self.tmpfname, 'wb') as fo:
            fo.write(b'0123456789')
        self.assertFileSize(10)

        a = bitarray()
        with open(self.tmpfname, 'wb') as fi:
            self.assertRaises(Exception, a.fromfile, fi)

        if is_py3k:
            with open(self.tmpfname, 'r') as fi:
                self.assertRaises(TypeError, a.fromfile, fi)

    def test_from_large_files(self):
        for N in range(65534, 65538):
            data = os.urandom(N)
            with open(self.tmpfname, 'wb') as fo:
                fo.write(data)

            a = bitarray()
            with open(self.tmpfname, 'rb') as fi:
                a.fromfile(fi)
            self.assertEqual(len(a), 8 * N)
            self.assertEqual(a.buffer_info()[1], N)
            self.assertEqual(a.tobytes(), data)

    def test_fromfile_extend_existing(self):
        with open(self.tmpfname, 'wb') as fo:
            fo.write(b'Foo')

        foo_le = '011000101111011011110110'
        a = bitarray('1', endian='little')
        with open(self.tmpfname, 'rb') as fi:
            a.fromfile(fi)

        self.assertEqual(a, bitarray('1' + foo_le))

        for n in range(20):
            a = bitarray(n, endian='little')
            a.setall(1)
            with open(self.tmpfname, 'rb') as fi:
                a.fromfile(fi)
            self.assertEqual(a, bitarray(n * '1' + foo_le))

    def test_fromfile_n(self):
        a = bitarray()
        a.frombytes(b'ABCDEFGHIJ')
        with open(self.tmpfname, 'wb') as fo:
            a.tofile(fo)
        self.assertFileSize(10)

        with open(self.tmpfname, 'rb') as f:
            a = bitarray()
            a.fromfile(f, 0);  self.assertEqual(a.tobytes(), b'')
            a.fromfile(f, 1);  self.assertEqual(a.tobytes(), b'A')
            f.read(1)  # skip B
            a.fromfile(f, 1);  self.assertEqual(a.tobytes(), b'AC')
            a = bitarray()
            a.fromfile(f, 2);  self.assertEqual(a.tobytes(), b'DE')
            a.fromfile(f, 1);  self.assertEqual(a.tobytes(), b'DEF')
            a.fromfile(f, 0);  self.assertEqual(a.tobytes(), b'DEF')
            a.fromfile(f);     self.assertEqual(a.tobytes(), b'DEFGHIJ')
            a.fromfile(f);     self.assertEqual(a.tobytes(), b'DEFGHIJ')

        a = bitarray()
        with open(self.tmpfname, 'rb') as f:
            f.read(1)
            self.assertRaises(EOFError, a.fromfile, f, 10)
        # check that although we received an EOFError, the bytes were read
        self.assertEqual(a.tobytes(), b'BCDEFGHIJ')

        a = bitarray()
        with open(self.tmpfname, 'rb') as f:
            # negative values - like ommiting the argument
            a.fromfile(f, -1)
            self.assertEqual(a.tobytes(), b'ABCDEFGHIJ')
            self.assertRaises(EOFError, a.fromfile, f, 1)

    def test_fromfile_BytesIO(self):
        f = BytesIO(b'somedata')
        a = bitarray()
        a.fromfile(f, 4)
        self.assertEqual(len(a), 32)
        self.assertEqual(a.tobytes(), b'some')
        a.fromfile(f)
        self.assertEqual(len(a), 64)
        self.assertEqual(a.tobytes(), b'somedata')

    def test_tofile_empty(self):
        a = bitarray()
        with open(self.tmpfname, 'wb') as f:
            a.tofile(f)

        self.assertFileSize(0)

    def test_tofile_Foo(self):
        a = bitarray('0100011' '001101111' '01101111', endian='big')
        b = a.copy()
        with open(self.tmpfname, 'wb') as f:
            a.tofile(f)
        self.assertEQUAL(a, b)

        self.assertFileSize(3)
        self.assertEqual(self.read_file(), b'Foo')

    def test_tofile_random(self):
        for a in self.randombitarrays():
            with open(self.tmpfname, 'wb') as fo:
                a.tofile(fo)
            n = bits2bytes(len(a))
            self.assertFileSize(n)
            raw = self.read_file()
            self.assertEqual(len(raw), n)
            self.assertEqual(raw, a.tobytes())

    def test_tofile_errors(self):
        n = 100
        a = bitarray(8 * n)
        self.assertRaises(TypeError, a.tofile)

        with open(self.tmpfname, 'wb') as f:
            a.tofile(f)
        self.assertFileSize(n)
        # write to closed file
        self.assertRaises(ValueError, a.tofile, f)

        if is_py3k:
            with open(self.tmpfname, 'w') as f:
                self.assertRaises(TypeError, a.tofile, f)

        with open(self.tmpfname, 'rb') as f:
            self.assertRaises(Exception, a.tofile, f)

    def test_tofile_large(self):
        n = 100 * 1000
        a = bitarray(8 * n)
        a.setall(0)
        a[2::37] = 1
        with open(self.tmpfname, 'wb') as f:
            a.tofile(f)
        self.assertFileSize(n)

        raw = self.read_file()
        self.assertEqual(len(raw), n)
        self.assertEqual(raw, a.tobytes())

    def test_tofile_ones(self):
        for n in range(20):
            a = n * bitarray('1', endian='little')
            with open(self.tmpfname, 'wb') as fo:
                a.tofile(fo)

            raw = self.read_file()
            self.assertEqual(len(raw), bits2bytes(len(a)))
            # when we the the unused bits in a, we can compare
            a.fill()
            b = bitarray(endian='little')
            b.frombytes(raw)
            self.assertEqual(a, b)

    def test_tofile_BytesIO(self):
        for n in list(range(10)) + list(range(65534, 65538)):
            data = os.urandom(n)
            a = bitarray(0, 'big')
            a.frombytes(data)
            self.assertEqual(len(a), 8 * n)
            f = BytesIO()
            a.tofile(f)
            self.assertEqual(f.getvalue(), data)


tests.append(FileTests)

# ----------------------------- Decode Tree ---------------------------------

alpabet_code = {
    ' ': bitarray('001'),         '.': bitarray('0101010'),
    'a': bitarray('0110'),        'b': bitarray('0001100'),
    'c': bitarray('000011'),      'd': bitarray('01011'),
    'e': bitarray('111'),         'f': bitarray('010100'),
    'g': bitarray('101000'),      'h': bitarray('00000'),
    'i': bitarray('1011'),        'j': bitarray('0111101111'),
    'k': bitarray('00011010'),    'l': bitarray('01110'),
    'm': bitarray('000111'),      'n': bitarray('1001'),
    'o': bitarray('1000'),        'p': bitarray('101001'),
    'q': bitarray('00001001101'), 'r': bitarray('1101'),
    's': bitarray('1100'),        't': bitarray('0100'),
    'u': bitarray('000100'),      'v': bitarray('0111100'),
    'w': bitarray('011111'),      'x': bitarray('0000100011'),
    'y': bitarray('101010'),      'z': bitarray('00011011110')
}

class DecodeTreeTests(unittest.TestCase):

    def test_create(self):
        dt = decodetree(alpabet_code)
        self.assertEqual(repr(type(dt)), "<%s 'bitarray.decodetree'>" %
                         ('class' if is_py3k else 'type'))
        self.assertRaises(TypeError, decodetree, None)
        self.assertRaises(TypeError, decodetree, 'foo')
        d = dict(alpabet_code)
        d['-'] = bitarray()
        self.assertRaises(ValueError, decodetree, d)

    def test_sizeof(self):
        dt = decodetree({'.': bitarray('1')})
        self.assertTrue(0 < sys.getsizeof(dt) < 100)

        dt = decodetree({'a': bitarray(20 * '0')})
        self.assertTrue(sys.getsizeof(dt) > 200)

    def test_nodes(self):
        for n in range(1, 20):
            dt = decodetree({'a': bitarray(n * '0')})
            self.assertEqual(dt.nodes(), n + 1)

        dt = decodetree({'I': bitarray('1'),   'l': bitarray('01'),
                         'a': bitarray('001'), 'n': bitarray('000')})
        self.assertEqual(dt.nodes(), 7)
        dt = decodetree(alpabet_code)
        self.assertEqual(dt.nodes(), 70)

    def test_todict(self):
        t = decodetree(alpabet_code)
        d = t.todict()
        self.assertEqual(d, alpabet_code)

    def test_decode(self):
        t = decodetree(alpabet_code)
        a = bitarray('10110111001101001')
        self.assertEqual(a.decode(t), ['i', 'l', 'a', 'n'])
        self.assertEqual(''.join(a.iterdecode(t)), 'ilan')
        a = bitarray()
        self.assertEqual(a.decode(t), [])
        self.assertEqual(''.join(a.iterdecode(t)), '')

    def test_large(self):
        d = {i: bitarray((1 << j) & i for j in range(10))
             for i in range(1024)}
        t = decodetree(d)
        self.assertEqual(t.todict(), d)
        self.assertEqual(t.nodes(), 2047)
        self.assertTrue(sys.getsizeof(t) > 10000)

tests.append(DecodeTreeTests)

# ------------------ variable length encoding and decoding ------------------

class PrefixCodeTests(unittest.TestCase, Util):

    def test_encode_string(self):
        a = bitarray()
        a.encode(alpabet_code, '')
        self.assertEqual(a, bitarray())
        a.encode(alpabet_code, 'a')
        self.assertEqual(a, bitarray('0110'))

    def test_encode_list(self):
        a = bitarray()
        a.encode(alpabet_code, [])
        self.assertEqual(a, bitarray())
        a.encode(alpabet_code, ['e'])
        self.assertEqual(a, bitarray('111'))

    def test_encode_iter(self):
        a = bitarray()
        d = {0: bitarray('0'), 1: bitarray('1')}
        a.encode(d, iter([0, 1, 1, 0]))
        self.assertEqual(a, bitarray('0110'))

        def foo():
            for c in 1, 1, 0, 0, 1, 1:
                yield c

        a.encode(d, foo())
        a.encode(d, range(2))
        self.assertEqual(a, bitarray('011011001101'))
        self.assertEqual(d, {0: bitarray('0'), 1: bitarray('1')})

    def test_encode_symbol_not_in_code(self):
        d = {None : bitarray('0'),
             0    : bitarray('10'),
             'A'  : bitarray('11')}
        a = bitarray()
        a.encode(d, ['A', None, 0])
        self.assertEqual(a, bitarray('11010'))
        self.assertRaises(ValueError, a.encode, d, [1, 2])
        self.assertRaises(ValueError, a.encode, d, 'ABCD')

    def test_encode_not_iterable(self):
        d = {'a': bitarray('0'), 'b': bitarray('1')}
        a = bitarray()
        a.encode(d, 'abba')
        self.assertRaises(TypeError, a.encode, d, 42)
        self.assertRaises(TypeError, a.encode, d, 1.3)
        self.assertRaises(TypeError, a.encode, d, None)
        self.assertEqual(a, bitarray('0110'))

    def test_check_codedict_encode(self):
        a = bitarray()
        self.assertRaises(TypeError, a.encode, None, '')
        self.assertRaises(ValueError, a.encode, {}, '')
        self.assertRaises(TypeError, a.encode, {'a': 'b'}, 'a')
        self.assertRaises(ValueError, a.encode, {'a': bitarray()}, 'a')
        self.assertEqual(len(a), 0)

    def test_check_codedict_decode(self):
        a = bitarray('101')
        self.assertRaises(TypeError, a.decode, 0)
        self.assertRaises(ValueError, a.decode, {})
        self.assertRaises(TypeError, a.decode, {'a': 42})
        self.assertRaises(ValueError, a.decode, {'a': bitarray()})
        self.assertEqual(a, bitarray('101'))

    def test_check_codedict_iterdecode(self):
        a = bitarray('1100101')
        self.assertRaises(TypeError, a.iterdecode, 0)
        self.assertRaises(ValueError, a.iterdecode, {})
        self.assertRaises(TypeError, a.iterdecode, {'a': []})
        self.assertRaises(ValueError, a.iterdecode, {'a': bitarray()})
        self.assertEqual(a, bitarray('1100101'))

    def test_decode_simple(self):
        d = {'I': bitarray('1'),   'l': bitarray('01'),
             'a': bitarray('001'), 'n': bitarray('000')}
        dcopy = dict(d)
        a = bitarray('101001000')
        self.assertEqual(a.decode(d), ['I', 'l', 'a', 'n'])
        self.assertEqual(d, dcopy)
        self.assertEqual(a, bitarray('101001000'))

    def test_iterdecode_simple(self):
        d = {'I': bitarray('1'),   'l': bitarray('01'),
             'a': bitarray('001'), 'n': bitarray('000')}
        dcopy = dict(d)
        a = bitarray('101001000')
        self.assertEqual(list(a.iterdecode(d)), ['I', 'l', 'a', 'n'])
        self.assertEqual(d, dcopy)
        self.assertEqual(a, bitarray('101001000'))

    def test_iterdecode_remove_tree(self):
        d = {'I': bitarray('1'),   'l': bitarray('01'),
             'a': bitarray('001'), 'n': bitarray('000')}
        t = decodetree(d)
        a = bitarray('101001000')
        it = a.iterdecode(t)
        del t
        self.assertEqual(''.join(it), "Ilan")

    def test_decode_empty(self):
        d = {'a': bitarray('1')}
        a = bitarray()
        self.assertEqual(a.decode(d), [])
        self.assertEqual(d, {'a': bitarray('1')})
        # test decode iterator
        self.assertEqual(list(a.iterdecode(d)), [])
        self.assertEqual(d, {'a': bitarray('1')})
        self.assertEqual(len(a), 0)

    def test_decode_no_term(self):
        d = {'a': bitarray('0'), 'b': bitarray('111')}
        a = bitarray('011')
        msg = "decoding not terminated"
        self.assertRaisesMessage(ValueError, msg, a.decode, d)
        self.assertRaisesMessage(ValueError, msg, a.iterdecode, d)
        t = decodetree(d)
        self.assertRaisesMessage(ValueError, msg, a.decode, t)
        self.assertRaisesMessage(ValueError, msg, a.iterdecode, t)

        self.assertEqual(a, bitarray('011'))
        self.assertEqual(d, {'a': bitarray('0'), 'b': bitarray('111')})
        self.assertEqual(t.todict(), d)

    def test_decode_buggybitarray(self):
        d = {'a': bitarray('0')}
        a = bitarray('1')
        msg = "prefix code does not match data in bitarray"
        self.assertRaisesMessage(ValueError, msg, a.decode, d)
        self.assertRaisesMessage(ValueError, msg, a.iterdecode, d)
        t = decodetree(d)
        self.assertRaisesMessage(ValueError, msg, a.decode, t)
        self.assertRaisesMessage(ValueError, msg, a.iterdecode, t)

        self.assertEqual(a, bitarray('1'))
        self.assertEqual(d, {'a': bitarray('0')})
        self.assertEqual(t.todict(), d)

    def test_iterdecode_no_term(self):
        d = {'a': bitarray('0'), 'b': bitarray('111')}
        a = bitarray('011')
        it = a.iterdecode(d)
        self.assertEqual(next(it), 'a')
        self.assertRaisesMessage(ValueError, "decoding not terminated",
                                 next, it)
        self.assertEqual(a, bitarray('011'))

    def test_iterdecode_buggybitarray(self):
        d = {'a': bitarray('0')}
        a = bitarray('1')
        it = a.iterdecode(d)
        self.assertRaises(ValueError, next, it)
        self.assertEqual(a, bitarray('1'))
        self.assertEqual(d, {'a': bitarray('0')})

    def test_decode_buggybitarray2(self):
        d = {'a': bitarray('00'), 'b': bitarray('01')}
        a = bitarray('1')
        self.assertRaises(ValueError, a.decode, d)
        t = decodetree(d)
        self.assertRaises(ValueError, a.decode, t)

        self.assertEqual(a, bitarray('1'))
        self.assertEqual(d, {'a': bitarray('00'), 'b': bitarray('01')})
        self.assertEqual(t.todict(), d)

    def test_iterdecode_buggybitarray2(self):
        d = {'a': bitarray('00'), 'b': bitarray('01')}
        a = bitarray('1')
        it = a.iterdecode(d)
        self.assertRaises(ValueError, next, it)
        self.assertEqual(a, bitarray('1'))

        t = decodetree(d)
        it = a.iterdecode(t)
        self.assertRaises(ValueError, next, it)

        self.assertEqual(a, bitarray('1'))
        self.assertEqual(d, {'a': bitarray('00'), 'b': bitarray('01')})
        self.assertEqual(t.todict(), d)

    def test_decode_ambiguous_code(self):
        for d in [
            {'a': bitarray('0'), 'b': bitarray('0'), 'c': bitarray('1')},
            {'a': bitarray('01'), 'b': bitarray('01'), 'c': bitarray('1')},
            {'a': bitarray('0'), 'b': bitarray('01')},
            {'a': bitarray('0'), 'b': bitarray('11'), 'c': bitarray('111')},
        ]:
            a = bitarray()
            msg = "prefix code ambiguous"
            self.assertRaisesMessage(ValueError, msg, a.decode, d)
            self.assertRaisesMessage(ValueError, msg, a.iterdecode, d)
            self.assertRaisesMessage(ValueError, msg, decodetree, d)

    def test_miscitems(self):
        d = {None : bitarray('00'),
             0    : bitarray('110'),
             1    : bitarray('111'),
             ''   : bitarray('010'),
             2    : bitarray('011')}
        a = bitarray()
        a.encode(d, [None, 0, 1, '', 2])
        self.assertEqual(a, bitarray('00110111010011'))
        self.assertEqual(a.decode(d), [None, 0, 1, '', 2])
        # iterator
        it = a.iterdecode(d)
        self.assertEqual(next(it), None)
        self.assertEqual(next(it), 0)
        self.assertEqual(next(it), 1)
        self.assertEqual(next(it), '')
        self.assertEqual(next(it), 2)
        self.assertStopIteration(it)

    def test_real_example(self):
        a = bitarray()
        message = 'the quick brown fox jumps over the lazy dog.'
        a.encode(alpabet_code, message)
        self.assertEqual(a, bitarray('01000000011100100001001101000100101100'
          '00110001101000100011001101100001111110010010101001000000010001100'
          '10111101111000100000111101001110000110000111100111110100101000000'
          '0111001011100110000110111101010100010101110001010000101010'))
        self.assertEqual(''.join(a.decode(alpabet_code)), message)
        self.assertEqual(''.join(a.iterdecode(alpabet_code)), message)
        t = decodetree(alpabet_code)
        self.assertEqual(''.join(a.decode(t)), message)
        self.assertEqual(''.join(a.iterdecode(t)), message)

tests.append(PrefixCodeTests)

# -------------------------- Buffer Interface -------------------------------

class BufferInterfaceTests(unittest.TestCase):

    def test_read_simple(self):
        a = bitarray('01000001' '01000010' '01000011', endian='big')
        v = memoryview(a)
        self.assertEqual(len(v), 3)
        self.assertEqual(v[0], 65 if is_py3k else 'A')
        self.assertEqual(v.tobytes(), b'ABC')
        a[13] = 1
        self.assertEqual(v.tobytes(), b'AFC')

    def test_read_random(self):
        a = bitarray()
        a.frombytes(os.urandom(100))
        v = memoryview(a)
        self.assertEqual(len(v), 100)
        b = a[34 * 8 : 67 * 8]
        self.assertEqual(v[34:67].tobytes(), b.tobytes())
        self.assertEqual(v.tobytes(), a.tobytes())

    def test_resize(self):
        a = bitarray('01000001' '01000010' '01000011', endian='big')
        v = memoryview(a)
        self.assertRaises(BufferError, a.append, 1)
        self.assertRaises(BufferError, a.clear)
        self.assertRaises(BufferError, a.__delitem__, slice(0, 8))
        self.assertEqual(v.tobytes(), a.tobytes())

    def test_write(self):
        a = bitarray(8000)
        a.setall(0)
        v = memoryview(a)
        self.assertFalse(v.readonly)
        v[500] = 255 if is_py3k else '\xff'
        self.assertEqual(a[3999:4009], bitarray('0111111110'))
        a[4003] = 0
        self.assertEqual(a[3999:4009], bitarray('0111011110'))
        v[301:304] = b'ABC'
        self.assertEqual(a[300 * 8 : 305 * 8].tobytes(), b'\x00ABC\x00')

    def test_write_py3(self):
        if not is_py3k:
            return
        a = bitarray(40)
        a.setall(0)
        m = memoryview(a)
        v = m[1:4]
        v[0] = 65
        v[1] = 66
        v[2] = 67
        self.assertEqual(a.tobytes(), b'\x00ABC\x00')


tests.append(BufferInterfaceTests)

# ---------------------------------------------------------------------------

class TestsFrozenbitarray(unittest.TestCase, Util):

    def test_init(self):
        a = frozenbitarray('110')
        self.assertEqual(a, bitarray('110'))
        self.assertEqual(a.to01(), '110')
        for endian in 'big', 'little':
            a = frozenbitarray(0, endian)
            self.assertEqual(a.endian(), endian)

    def test_methods(self):
        # test a few methods which do not raise the TypeError
        a = frozenbitarray('1101100')
        self.assertEqual(a[2], 0)
        self.assertEqual(a[:4].to01(), '1101')
        self.assertEqual(a.count(), 4)
        self.assertEqual(a.index(0), 2)
        b = a.copy()
        self.assertEqual(b, a)
        self.assertEqual(repr(type(b)), "<class 'bitarray.frozenbitarray'>")
        self.assertEqual(len(b), 7)
        self.assertEqual(b.all(), False)
        self.assertEqual(b.any(), True)

    def test_init_from_bitarray(self):
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
        a = frozenbitarray('10111')
        self.assertEqual(repr(a), "frozenbitarray('10111')")
        self.assertEqual(str(a), "frozenbitarray('10111')")

    def test_immutable(self):
        a = frozenbitarray('111')
        self.assertRaises(TypeError, a.append, True)
        self.assertRaises(TypeError, a.clear)
        self.assertRaises(TypeError, a.__delitem__, 0)
        self.assertRaises(TypeError, a.__setitem__, 0, 0)

    def test_dictkey(self):
        a = frozenbitarray('01')
        b = frozenbitarray('1001')
        d = {a: 123, b: 345}
        self.assertEqual(d[frozenbitarray('01')], 123)
        self.assertEqual(d[frozenbitarray(b)], 345)

    def test_dictkey2(self):  # taken slightly modified from issue #74
        a1 = frozenbitarray([True, False])
        a2 = frozenbitarray([False, False])
        dct = {a1: "one", a2: "two"}
        a3 = frozenbitarray([True, False])
        self.assertEqual(a3, a1)
        self.assertEqual(dct[a3], 'one')

    def test_mix(self):
        a = bitarray('110')
        b = frozenbitarray('0011')
        self.assertEqual(a + b, bitarray('1100011'))
        a.extend(b)
        self.assertEqual(a, bitarray('1100011'))

    def test_pickle(self):
        for a in self.randombitarrays():
            f = frozenbitarray(a)
            g = pickle.loads(pickle.dumps(f))
            self.assertEqual(f, g)
            self.assertEqual(f.endian(), g.endian())
            self.assertTrue(str(g).startswith('frozenbitarray'))


tests.append(TestsFrozenbitarray)

# ---------------------------------------------------------------------------

def run(verbosity=1, repeat=1):
    import bitarray.test_util as btu
    tests.extend(btu.tests)

    print('bitarray is installed in: %s' % os.path.dirname(__file__))
    print('bitarray version: %s' % __version__)
    print('sys.version: %s' % sys.version)
    print('sys.prefix: %s' % sys.prefix)
    print('pointer size: %d bit' % (8 * _sysinfo()[0]))
    suite = unittest.TestSuite()
    for cls in tests:
        for _ in range(repeat):
            suite.addTest(unittest.makeSuite(cls))

    runner = unittest.TextTestRunner(verbosity=verbosity)
    return runner.run(suite)


if __name__ == '__main__':
    run()
