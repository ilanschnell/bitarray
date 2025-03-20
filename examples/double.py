from struct import pack, unpack

from bitarray import bitarray
from bitarray.util import ba2int, int2ba


class Double:

    def __init__(self, x=0.0):
        if isinstance(x, float):
            self.from_float(x)
        elif isinstance(x, str):
            self.from_string(x)
        else:
            raise TypeError("float or str expected")

    def __float__(self):
        a = self.to_bitarray()
        return unpack("<d", a.tobytes())[0]

    def __str__(self):
        a = self.to_bitarray()
        a.reverse()
        return "%s %s %s" % (a[0], a[1:12].to01(), a[12:].to01())

    def __repr__(self):
        return 'Double("%s")' % str(self)

    def from_float(self, x):
        a = bitarray(endian="little")
        a.frombytes(pack("<d", x))
        self.from_bitarray(a)

    def from_string(self, s):
        a = bitarray(s, endian="little")
        if len(a) != 64:
            raise ValueError("64 bits expected")
        a.reverse()
        self.from_bitarray(a)

    def from_bitarray(self, a):
        if len(a) != 64 or a.endian() != "little":
            raise ValueError("litten endian bitarray of length 64 expected")
        self.sign = a[63]
        self.exponent = ba2int(a[52:63]) - 1023
        self.fraction = a[0:52]

    def to_bitarray(self):
        if len(self.fraction) != 52:
            raise ValueError("fraction must be a bitarray of length 52")
        a = bitarray(self.fraction, endian="little")
        a.extend(int2ba(self.exponent + 1023, length=11, endian="little"))
        a.append(self.sign)
        return a

    def info(self):
        print("float: %r" % float(self))
        print(str(self))
        print("sign     = %d" % self.sign)
        print("exponent = %d" % self.exponent)
        d = Double()
        d.exponent = 0
        d.fraction = self.fraction
        x = float(d)
        if self.exponent == -1023:
            x -= 1
        print("fraction = %.17f" % x)
        exponent = self.exponent
        if exponent == -1023:
            exponent = -1022
        x *= pow(2.0, exponent)
        if self.sign:
            x = -x
        print("  --> %r" % x)


# ---------------------------------------------------------------------------

from math import pi, inf, nan, isnan
from random import getrandbits, randint
import unittest

from bitarray.util import urandom


EXAMPLES = [
    ( 0.0, "0 00000000000 " + 52 * "0"),
    ( 1.0, "0 01111111111 " + 52 * "0"),
    ( 1.5, "0 01111111111 1" + 51 * "0"),
    ( 2.0, "0 10000000000 " + 52 * "0"),
    ( 5.0, "0 10000000001 01" + 50 * "0"),
    (-5.0, "1 10000000001 01" + 50 * "0"),
    # smallest number > 1
    (1.0000000000000002, "0 01111111111 " + 51 * "0" + "1"),
    # minimal subnormal double
    (4.9406564584124654e-324, "0 00000000000 " + 51 * "0" + "1"),
    # maximal subnormal double
    (2.2250738585072009e-308, "0 00000000000 " + 52 * "1"),
    # minimal normal double
    (2.2250738585072014e-308, "0 00000000001 " + 52 * "0"),
    # maximal (normal) double
    (1.7976931348623157e+308, "0 11111111110 " + 52 * "1"),
    ( inf, "0 11111111111 " + 52 * "0"),
    (-inf, "1 11111111111 " + 52 * "0"),
    ( 1/3, "0 01111111101 " + 26 * "01"),
    (  pi, "0 10000000000 "
       "1001001000011111101101010100010001000010110100011000"),
    # largest number exactly representated as integer
    (2 ** 53 - 1.0, "0 10000110011 " + 52 * "1"),
]

class DoubleTests(unittest.TestCase):

    def test_zero(self):
        d = Double()
        self.assertEqual(float(d), 0.0)
        self.assertEqual(d.sign, 0)
        self.assertEqual(d.exponent, -1023)
        self.assertEqual(d.fraction, bitarray(52))

    def test_examples(self):
        for x, s in EXAMPLES:
            for d in Double(x), Double(s):
                self.assertEqual(float(d), x)
                self.assertEqual(str(d), s)

    def test_nan(self):
        s = "0 11111111111 1" + 51 * "0"
        for x in nan, s:
            d = Double(x)
            self.assertEqual(str(d), s)
            self.assertTrue(isnan(float(d)))

    def test_nan_msg(self):
        msg = urandom(52)
        d = Double()
        d.exponent = 1024
        d.fraction = msg
        x = float(d)
        self.assertIsInstance(x, float)
        self.assertTrue(isnan(x))
        e = Double(x)
        self.assertEqual(e.exponent, 1024)
        self.assertEqual(e.fraction, msg)

    def test_exponent52(self):
        for _ in range(1000):
            d = Double()
            d.fraction = urandom(52, endian="little")
            d.exponent = 52
            d.sign = getrandbits(1)
            i = (1 << 52) + ba2int(d.fraction)
            if d.sign:
                i = -i
            self.assertEqual(float(d), i)

    def test_exact_ints(self):
        for _ in range(1000):
            i = getrandbits(randint(1, 53))
            if i == 0:
                continue

            d = Double(float(i))
            self.assertEqual(d.sign, 0)

            a = int2ba(i, endian="little")
            a.pop()
            n = len(a)
            self.assertEqual(d.exponent, n)

            a = bitarray(52 - n, endian="little") + a
            self.assertEqual(d.fraction, a)

if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            d = Double(float(eval(arg)))
            d.info()
    else:
        unittest.main()
