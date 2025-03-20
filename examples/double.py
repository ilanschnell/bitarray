from struct import pack, unpack

from bitarray import bitarray


class Double:

    def __init__(self, x=0.0):
        if isinstance(x, float):
            self.from_float(x)
        elif isinstance(x, str):
            self.from_string(x)
        else:
            raise TypeError("float or str expected")

    def from_float(self, x):
        self.a = bitarray(endian="little")
        self.a.frombytes(pack("<d", x))

    def from_string(self, s):
        a = bitarray(s, endian="little")
        if len(a) != 64:
            raise ValueError("64 bits expected")
        a.reverse()
        self.a = a

    def __float__(self):
        return unpack("<d", self.a.tobytes())[0]

    def __str__(self):
        a = self.a[::-1]
        return "%s %s %s" % (a[0], a[1:12].to01(), a[12:].to01())

    def __repr__(self):
        return 'Double("%s")' % str(self)


# ---------------------------------------------------------------------------

from math import pi, inf, nan, isnan
import unittest

EXAMPLES = [
    ( 0.0, "0 00000000000 " + 52 * "0"),
    ( 1.0, "0 01111111111 " + 52 * "0"),
    ( 2.0, "0 10000000000 " + 52 * "0"),
    ( 5.0, "0 10000000001 0100" + 48 * "0"),
    (-5.0, "1 10000000001 0100" + 48 * "0"),
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
]

class DoubleTests(unittest.TestCase):

    def test_zero(self):
        d = Double()
        self.assertEqual(d.a, bitarray(64))
        self.assertEqual(float(d), 0.0)

    def test_examples(self):
        for x, s in EXAMPLES:
            for d in Double(x), Double(s):
                self.assertEqual(float(d), x)
                self.assertEqual(str(d), s)

    def test_nan(self):
        s = "0 11111111111 1000" + 48 * "0"
        for x in nan, s:
            d = Double(x)
            self.assertEqual(str(d), s)
            self.assertTrue(isnan(float(d)))


if __name__ == '__main__':
    unittest.main()
