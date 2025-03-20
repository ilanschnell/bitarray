from struct import pack, unpack

from bitarray import bitarray


class Double:

    def __init__(self, x=0.0):
        if isinstance(x, float):
            self.from_float(x)
        elif isinstance(x, str):
            self.from_str(x)
        else:
            raise TypeError("float or str expected")

    def from_float(self, x):
        self.a = bitarray(endian="little")
        self.a.frombytes(pack("<d", x))

    def from_str(self, s):
        sign, expo, frac = s.split()
        a = bitarray(endian="little")
        a.append(int(sign))
        a.extend(expo)
        if len(a) != 12:
            raise ValueError("11 bits in exponent expected")
        a.extend(frac)
        if len(a) != 64:
            raise ValueError("52 bits in fraction expected")
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

from math import inf, nan, isnan
import unittest


class DoubleTests(unittest.TestCase):

    def test_zero(self):
        d = Double()
        self.assertEqual(d.a, bitarray(64))
        self.assertEqual(float(d), 0.0)

    def test_examples(self):
        for x, s in [
                ( 0.0, "0 00000000000 " + 52 * "0"),
                ( 1.0, "0 01111111111 " + 52 * "0"),
                ( 5.0, "0 10000000001 0100" + 48 * "0"),
                (-5.0, "1 10000000001 0100" + 48 * "0"),
                ( 1/3, "0 01111111101 " + 26 * "01"),
                ( inf, "0 11111111111 " + 52 * "0"),
                (-inf, "1 11111111111 " + 52 * "0"),
                (2.225073858507201e-308, "0 00000000000 " + 52 * "1"),
                (4.940656458412465e-324, "0 00000000000 " + 51 * "0" + "1"),
        ]:
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
