from math import inf, nan, ldexp
from struct import pack, unpack

from bitarray import bitarray
from bitarray.util import ba2int, int2ba
from bitarray.bitfields import compile


class IEEEFloat:

    def __init__(self, x=0.0):
        if isinstance(x, (float, int)):
            self.from_float(float(x))
        elif isinstance(x, str):
            self.from_string(x)
        else:
            raise TypeError("float or str expected, got %r" % type(x).__name__)

    def __float__(self):
        a = self.to_bitarray()
        return unpack("<" + self.struct_format, a)[0]

    def __str__(self):
        a = self.to_bitarray()
        a.reverse()
        s = a.to01()
        i = 1 + self.exponent_bits
        return "%s %s %s" % (s[0], s[1:i], s[i:])

    def __repr__(self):
        return '%s("%s")' % (type(self).__name__, self)

    def from_float(self, x):
        a = bitarray(pack("<" + self.struct_format, x), endian="little")
        self.from_bitarray(a)

    def from_string(self, s):
        a = bitarray(s, endian="little")
        a.reverse()
        self.from_bitarray(a)

    @property
    def cf(self):
        return compile("<b%d u%d u" % (self.fraction_bits, self.exponent_bits))

    def from_bitarray(self, a):
        self.fraction, self.exponent, self.sign = self.cf.unpack(a)
        self.exponent -= self.exponent_bias

    def to_bitarray(self):
        return self.cf.pack(self.fraction,
                            self.exponent + self.exponent_bias, self.sign)

    def unpack(self):
        if self.exponent == self.exponent_bias + 1:
            if self.fraction.any():
                return nan
            return -inf if self.sign else inf

        x = ba2int(self.fraction) / (1 << self.fraction_bits)
        exponent = self.exponent
        if exponent == -self.exponent_bias:
            # Subnormals and zero use exponent 1 - bias.
            exponent += 1
        else:
            # Normal numbers have an implicit leading 1.
            x += 1
        x = ldexp(x, exponent)
        if self.sign:
            x = -x
        return x

    def info(self):
        print("float: %s ======== IEEE %d-bit" % (float(self), self.nbits))
        print(str(self))
        print("sign     = %d" % self.sign)
        print("exponent = %d" % self.exponent)

        if self.exponent == self.exponent_bias + 1:
            print("fraction = %s" % self.fraction[::-1].to01())
            print("  --> %s" % self.unpack())
            return

        x = ba2int(self.fraction) / (1 << self.fraction_bits)
        if self.exponent != -self.exponent_bias:
            x += 1

        print("fraction = %.*f" % (self.decimal_digits, x))
        print("  --> %s" % self.unpack())


class Half(IEEEFloat):
    nbits = 16
    struct_format = "e"
    exponent_bits = 5
    fraction_bits = 10
    exponent_bias = 15
    decimal_digits = 5


class Single(IEEEFloat):
    nbits = 32
    struct_format = "f"
    exponent_bits = 8
    fraction_bits = 23
    exponent_bias = 127
    decimal_digits = 9


class Double(IEEEFloat):
    nbits = 64
    struct_format = "d"
    exponent_bits = 11
    fraction_bits = 52
    exponent_bias = 1023
    decimal_digits = 17


# ---------------------------------------------------------------------------

from math import pi, isnan, log10
from random import getrandbits, randint
import unittest

from bitarray.util import urandom, gen_primes, zeros


FLOAT_TYPES = Half, Single, Double

EXAMPLES_16 = [
    (0.0, "0 00000 0000000000"),
    # smallest positive subnormal number
    (5.960464477539063e-08, "0 00000 0000000001"),
    # largest subnormal number
    (6.097555160522461e-05, "0 00000 1111111111"),
    # smallest positive normal number
    (6.103515625e-05, "0 00001 0000000000"),
    # nearest value to 1/3
    (0.333251953125, "0 01101 0101010101"),
    # largest number less than one
    (0.99951171875, "0 01110 1111111111"),
    (1.0,           "0 01111 0000000000"),
    # smallest number larger than one
    (1.0009765625, "0 01111 0000000001"),
    # closest value to pi
    (3.140625, "0 10000 1001001000"),
    # largest exactly representable odd integer
    (2 ** 11 - 1, "0 11001 1111111111"),
    # largest finite number
    (65504.0, "0 11110 1111111111"),
    # infinity
    ( inf, "0 11111 0000000000"),
    (-inf, "1 11111 0000000000"),
    (-2.0, "1 10000 0000000000"),
    (29.0, "0 10011 1101000000"),
    (-0.0, "1 00000 0000000000"),
]

EXAMPLES_32 = [
    (0.0, "0 00000000 00000000000000000000000"),
    # smallest positive subnormal number
    (1.401298464324817e-45,  "0 00000000 00000000000000000000001"),
    # largest subnormal number
    (1.1754942106924411e-38, "0 00000000 11111111111111111111111"),
    # smallest positive normal number
    (1.1754943508222875e-38, "0 00000001 00000000000000000000000"),
    # nearest value to 1/3
    (0.3333333432674408, "0 01111101 01010101010101010101011"),
    # largest number less than one
    (0.9999999403953552, "0 01111110 11111111111111111111111"),
    (1.0,                "0 01111111 00000000000000000000000"),
    # smallest number larger than one
    (1.0000001192092896, "0 01111111 00000000000000000000001"),
    # closest value to pi
    (3.1415927410125732, "0 10000000 10010010000111111011011"),
    # largest exactly representable odd integer
    (2 ** 24 - 1, "0 10010110 11111111111111111111111"),
    # largest finite number
    (3.4028234663852886e+38, "0 11111110 11111111111111111111111"),
    # infinity
    ( inf, "0 11111111 00000000..."),
    (-inf, "1 11111111 00000000..."),
    (-2.0, "1 10000000 00000000..."),
    (29.0, "0 10000011 11010000..."),
    (-0.0, "1 00000000 00000000..."),
]

EXAMPLES_64 = [
    (0.0, "0 00000000000 00000000..."),
    # smallest positive subnormal number
    (4.9406564584124654e-324, "0 00000000000 " + 51 * "0" + "1"),
    # largest subnormal number
    (2.2250738585072009e-308, "0 00000000000 1111111..."),
    # smallest positive normal number
    (2.2250738585072014e-308, "0 00000000001 0000000..."),
    # nearest value to 1/3
    (1/3, "0 01111111101 " + 26 * "01"),
    # largest number less than one
    (0.9999999999999999, "0 01111111110 1111111..."),
    (1.0,                "0 01111111111 0000000..."),
    # smallest number larger than one
    (1.0000000000000002, "0 01111111111 " + 51 * "0" + "1"),
    # closest value to pi
    (pi, "0 10000000000 1001001000011111101101010100010001000010110100011000"),
    # largest exactly representable odd integer
    (2 ** 53 - 1, "0 10000110011 11111..."),
    # largest finite number
    (1.7976931348623157e+308, "0 11111111110 1111111..."),
    # infinity
    ( inf, "0 11111111111 00000000..."),
    (-inf, "1 11111111111 00000000..."),
    (-2.0, "1 10000000000 00000000..."),
    (29.0, "0 10000000011 11010000..."),
    (-0.0, "1 00000000000 00000000..."),
]


class IEEEFloatTests(unittest.TestCase):

    def test_parameters(self):
        for cls in FLOAT_TYPES:
            self.assertEqual(cls.nbits,
                             1 + cls.exponent_bits + cls.fraction_bits)
            self.assertEqual(cls.exponent_bias + 1,
                             1 << (cls.exponent_bits - 1))
            self.assertEqual(cls.decimal_digits,
                             int((cls.fraction_bits + 1) * log10(2)) + 2)

    def test_zero(self):
        for cls in FLOAT_TYPES:
            with self.subTest(cls=cls.__name__):
                x = cls()
                self.assertEqual(x.cf.width, x.nbits)
                self.assertEqual(float(x), 0.0)
                self.assertEqual(x.sign, 0)
                self.assertEqual(x.exponent, -cls.exponent_bias)
                self.assertEqual(x.fraction, zeros(cls.fraction_bits))
                self.assertEqual(x.to_bitarray(), zeros(cls.nbits))

    def test_random_bit_patterns(self):
        for cls in FLOAT_TYPES:
            for _ in range(1000):
                f = cls()
                f.from_bitarray(urandom(cls.nbits,
                                        ["little", "big"][getrandbits(1)]))
                s = str(f)
                self.assertEqual(str(cls(s)), s)
                x = float(f)
                if isnan(x):
                    continue
                self.assertEqual(str(cls(x)), s)
                self.assertEqual(f.unpack(), x)

    def test_examples(self):
        for cls, examples in [
                (Half, EXAMPLES_16),
                (Single, EXAMPLES_32),
                (Double, EXAMPLES_64),
        ]:
            for value, s in examples:
                if s.endswith("..."):
                    s = s[:-3].ljust(cls.nbits + 2, s[-4])
                for x in cls(value), cls(s):
                    self.assertEqual(float(x), value)
                    self.assertEqual(x.unpack(), value)
                    self.assertEqual(str(x), s)

    def test_numberphile(self):
        # https://www.youtube.com/watch?v=c066hLi78B0
        x = Double(0.41468_25098_51111_66024)
        self.assertEqual(x.sign, 0)
        self.assertEqual(x.exponent, -2)
        self.assertEqual(x.fraction[::-1], gen_primes(55)[3:])

    def test_nan(self):
        for cls in FLOAT_TYPES:
            for sign in 0, 1:
                s = "%d %s 1%s" % (sign,
                                   cls.exponent_bits * "1",
                                   (cls.fraction_bits - 1) * "0")
                for value in nan, s:
                    x = cls(value)
                    self.assertTrue(isnan(float(x)))
                    self.assertTrue(isnan(x.unpack()))

    def test_nan_msg(self):
        msg = urandom(Double.fraction_bits)
        x = Double()
        x.exponent = Double.exponent_bias + 1
        x.fraction = msg
        value = float(x)
        self.assertIs(type(value), float)
        self.assertTrue(isnan(value))
        y = Double(value)
        self.assertEqual(y.exponent, Double.exponent_bias + 1)
        self.assertEqual(y.fraction, msg)

    def test_inf(self):
        for cls in FLOAT_TYPES:
            for sign in 0, 1:
                s = "%d %s %s" % (sign,
                                  cls.exponent_bits * "1",
                                  cls.fraction_bits * "0")
                expected = -inf if sign else inf
                for value in expected, s:
                    f = cls(value)
                    self.assertEqual(float(f), expected)
                    self.assertEqual(f.unpack(), expected)

    def test_exact_ints(self):
        for cls in FLOAT_TYPES:
            fb = cls.fraction_bits
            for _ in range(1000):
                x = cls()
                x.fraction = urandom(fb, endian="little")
                x.exponent = fb
                x.sign = getrandbits(1)
                value = (1 << fb) + ba2int(x.fraction)
                if x.sign:
                    value = -value
                self.assertEqual(float(x), value)
                self.assertEqual(x.unpack(), value)

            for _ in range(1000):
                value = getrandbits(randint(1, fb + 1))
                if value == 0:
                    continue
                x = cls(value)
                self.assertEqual(x.sign, 0)
                a = int2ba(value, endian="little")
                self.assertEqual(a.pop(), 1)
                n = len(a)
                self.assertEqual(x.exponent, n)
                self.assertEqual(x.fraction, zeros(fb - n, "little") + a)


if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            o = eval(arg)
            x: IEEEFloat
            if isinstance(o, str):
                nbits = len(bitarray(o))
                if nbits == 16:
                    x = Half(o)
                elif nbits == 32:
                    x = Single(o)
                elif nbits == 64:
                    x = Double(o)
                else:
                    raise ValueError("Did not expect %d bits" % nbits)
                x.info()
            else:
                for cls in FLOAT_TYPES:
                    x = cls(o)
                    x.info()
    else:
        unittest.main()
