"""
Pack and unpack bit-level structures using `bitarray`.

A format is a whitespace-separated sequence of fields.  Compile the format
once, then use the resulting `Struct` instance to pack values into a bitarray
or unpack a bitarray into a tuple::

    from bitarray import bitarray
    from bitfields import compile

    cf = compile("u3 s5 x2 b4 B16 f32")
    values = (5, -3, bitarray("1010", endian="big"), b"AB", 1.5)
    a = cf.pack(*values, endian="big")
    assert cf.unpack(a) == values

Padding fields do not consume or produce values.  The input to `unpack()`
must have exactly `cf.width()` bits.  `cf.values()` gives the number of
values consumed by `pack()` and returned by `unpack()`.

The supported field codes are:

u   An unsigned integer stored in the field width.
s   A signed integer stored in the field width using two's-complement
    representation.
f   An IEEE floating-point value.  The width must be 16, 32, or 64.
b   A bitarray matching the field width.
B   A bytes or bytearray value occupying the field width.  The width must be
    a multiple of eight; unpacking always returns `bytes`.
x   Zero-padding bits.  Padding is checked while unpacking.
X   One-padding bits.  Padding is checked while unpacking.

The width *N* defaults to one when omitted.  A field may be prefixed by a
repeat count: for example, `3u2` is equivalent to `u2 u2 u2`.  The
`endian` argument to `pack()` controls the bit order and the numeric byte
order of integer and floating-point fields.
"""

import re
import struct
from dataclasses import dataclass

from bitarray import bitarray
from bitarray.util import int2ba, ba2int


@dataclass(frozen=True)
class Field:

    width: int
    consumes_value = True
    code = ""

    def format(self):
        return "%s%d" % (self.code, self.width)


@dataclass(frozen=True)
class IntField(Field):

    signed: bool

    def __post_init__(self):
        if self.width == 0:
            raise ValueError("integer width cannot be zero")

    @property
    def code(self):
        return "s" if self.signed else "u"

    def pack(self, value, endian):
        return int2ba(value, length=self.width, endian=endian,
                      signed=self.signed)

    def unpack(self, a):
        return ba2int(a, signed=self.signed)


@dataclass(frozen=True)
class FloatField(Field):

    code = "f"
    formats = {16: "e", 32: "f", 64: "d"}

    def __post_init__(self):
        if self.width not in self.formats:
            raise ValueError("float must have width 16, 32 or 64, got %d" %
                             self.width)

    def struct_format(self, endian):
        return ("<" if endian == "little" else ">") + self.formats[self.width]

    def pack(self, value, endian):
        return bitarray(struct.pack(self.struct_format(endian), value),
                        endian=endian)

    def unpack(self, a):
        return struct.unpack(self.struct_format(a.endian), bytes(a))[0]


@dataclass(frozen=True)
class BitsField(Field):

    code = "b"

    def pack(self, a, endian):
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width:
            raise ValueError("bitarray of length %d expected" % self.width)
        return a

    def unpack(self, a):
        return a


@dataclass(frozen=True)
class BytesField(Field):

    code = "B"

    def __post_init__(self):
        if self.width % 8:
            raise ValueError("width not a multiple of 8")

    def pack(self, value, endian):
        if not isinstance(value, (bytes, bytearray)):
            raise TypeError("bytes expected, got %r" % type(value).__name__)
        if len(value) != self.width // 8:
            raise ValueError("bytes of length %d expected" % (self.width // 8))
        a = bitarray(0, endian)
        a.frombytes(value)
        return a

    def unpack(self, a):
        return bytes(a)


@dataclass(frozen=True)
class PaddingField(Field):

    value: bool
    consumes_value = False

    @property
    def code(self):
        return "X" if self.value else "x"

    def _bits(self):
        return self.width * bitarray("1" if self.value else "0")

    def pack(self, value, endian):
        return self._bits()

    def unpack(self, a):
        if a != self._bits():
            raise ValueError("'%s' padding expected, got '%s'" %
                             (self._bits().to01(), a.to01()))


class Struct:

    pat = re.compile(r"(\d*)(\w)(\d*)")

    def __init__(self, format=""):
        fields = []
        for s in format.split():
            match = self.pat.fullmatch(s)
            if match is None:
                raise ValueError("invalid format %r" % s)
            n = int(match.group(1) or 1)
            c = match.group(2)
            m = int(match.group(3) or 1)
            fields.extend(self.field_from_code(c, m) for _ in range(n))
        self.fields = tuple(fields)

    @staticmethod
    def field_from_code(code, width):
        if code in "us":
            return IntField(width, signed=(code == "s"))
        if code == "f":
            return FloatField(width)
        if code == "b":
            return BitsField(width)
        if code == "B":
            return BytesField(width)
        if code in "xX":
            return PaddingField(width, value=(code == "X"))
        raise ValueError("Not a valid code: %r" % code)

    def width(self):
        return sum(field.width for field in self.fields)

    def values(self):
        return sum(field.consumes_value for field in self.fields)

    def format(self):
        return " ".join(field.format() for field in self.fields)

    def pack(self, *values, endian=None):
        if len(values) != self.values():
            raise ValueError("expected %d values to pack, got %d" %
                             (self.values(), len(values)))
        a = bitarray(0, endian)
        i = 0  # value index
        for field in self.fields:
            value = None
            if field.consumes_value:
                value = values[i]
                i += 1
            a.extend(field.pack(value, a.endian))
        return a

    def unpack(self, a):
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width():
            raise ValueError("expected bitarray of length %d, got %d" %
                             (self.width(), len(a)))
        i = 0
        res = []
        for field in self.fields:
            j = i + field.width
            b = a[i:j]
            value = field.unpack(b)
            if field.consumes_value:
                res.append(value)
            i = j
        return tuple(res)


def compile(format):
    return Struct(format)


# ---------------------------------------------------------------------------

import math
import unittest


class StructTests(unittest.TestCase):

    def test_example(self):
        cf = compile("3u2 4x s7 3x X3 u b5 B16 f16")
        self.assertEqual(cf.width(), 61)
        self.assertEqual(cf.values(), 8)
        self.assertEqual(cf.format(),
                         "u2 u2 u2 x1 x1 x1 x1 s7 x1 x1 x1 X3 u1 b5 B16 f16")
        values = 1, 2, 3, -2, 1, bitarray("01110"), b"A\xff", -29.0
        a = cf.pack(*values, endian="big")
        self.assertEqual(len(a), 61)
        self.assertEqual(cf.unpack(a), values)

    def test_format(self):
        self.assertRaises(ValueError, compile, "u8junk")
        self.assertRaises(ValueError, compile, "!")
        self.assertRaises(ValueError, compile, "q8")
        self.assertEqual(compile("3u2").format(), "u2 u2 u2")
        self.assertEqual(compile("u").format(), "u1")

    def test_format_empty(self):
        cf = compile("")
        self.assertEqual(cf.width(), 0)
        self.assertEqual(cf.values(), 0)
        self.assertEqual(cf.pack(), bitarray())
        self.assertEqual(cf.unpack(bitarray()), ())

    def test_pack_value_count(self):
        cf = compile("u8 s8")
        self.assertRaises(ValueError, cf.pack, 1)
        self.assertRaises(ValueError, cf.pack, 1, 2, 3)
        self.assertRaises(ValueError, compile("3x").pack, 1)

    def test_unpack_errors(self):
        cf = compile("u8")
        self.assertRaises(ValueError, cf.unpack, bitarray(7))
        self.assertRaises(ValueError, cf.unpack, bitarray(9))
        self.assertRaises(TypeError, cf.unpack, [0, 1, 0, 0, 1, 1, 1, 1])

    def test_unsigned_int(self):
        cf = compile("u20")
        self.assertEqual(cf.width(), 20)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(1 << 19, endian="little")
        self.assertEqual(a.endian, "little")
        self.assertEqual(len(a), 20)
        self.assertEqual(a.count(), 1)
        self.assertEqual(a[19], 1)
        self.assertEqual(cf.unpack(a), (1 << 19, ))
        self.assertRaises(OverflowError, cf.pack, -1)
        self.assertRaises(OverflowError, cf.pack, 1 << 20)
        self.assertRaises(TypeError, cf.pack, 1.0)
        self.assertRaises(ValueError, compile, "u0")

    def test_signed_int(self):
        cf = compile("s10")
        self.assertEqual(cf.width(), 10)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(-1, endian="little")
        self.assertEqual(a.endian, "little")
        self.assertEqual(a.to01(), 10 * "1")
        self.assertEqual(cf.unpack(a), (-1, ))
        self.assertRaises(OverflowError, cf.pack, -513)
        self.assertRaises(OverflowError, cf.pack, 512)
        self.assertRaises(TypeError, cf.pack, -2.0)
        self.assertRaises(ValueError, compile, "s0")

    def test_float16(self):
        cf = compile("f16")
        self.assertEqual(cf.width(), 16)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(1.25, endian="little")
        self.assertEqual(a.endian, "little")
        self.assertEqual(a, bitarray("0000000010 11110 0"))
        self.assertEqual(cf.unpack(a), (1.25, ))
        a = cf.pack(-3.0, endian="big")
        self.assertEqual(a.endian, "big")
        self.assertEqual(a, bitarray("1 10000 1000000000"))
        self.assertEqual(cf.unpack(a), (-3.0, ))

    def test_float32(self):
        cf = compile("f32")
        self.assertEqual(cf.width(), 32)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(1.0, endian="big")
        self.assertEqual(a, bitarray("0 01111111 00000000000000000000000"))

    def test_float64(self):
        cf = compile("f64")
        self.assertEqual(cf.width(), 64)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(0.0, endian="big")
        self.assertEqual(a, 64 * bitarray("0"))

    # list of (nbits, exponent bits)
    float_sizes = [(16, 5), (32, 8), (64, 11)]

    def test_float_special(self):
        for nbits, exp_bits in self.float_sizes:
            cf = compile("f%d" % nbits)
            self.assertEqual(cf.width(), nbits)
            self.assertEqual(cf.values(), 1)
            # -0.0
            a = cf.pack(-0.0, endian="big")
            self.assertEqual(len(a), nbits)
            s = "1 %s" % ((nbits - 1) * "0")
            self.assertEqual(a, bitarray(s))
            x = cf.unpack(a)[0]
            self.assertEqual(x, 0.0)
            self.assertEqual(math.copysign(1.0, x), -1.0)
            # infinity
            a = cf.pack(float("inf"), endian="big")
            self.assertEqual(len(a), nbits)
            s = "0 %s %s" % (exp_bits * "1", (nbits - exp_bits - 1) * "0")
            self.assertEqual(a, bitarray(s))
            self.assertEqual(cf.unpack(a), (float('inf'), ))
            # nan
            a = cf.pack(float("nan"), endian="big")
            self.assertEqual(len(a), nbits)
            s = "0 %s 1%s" % (exp_bits * "1", (nbits - exp_bits - 2) * "0")
            self.assertEqual(a, bitarray(s))
            self.assertTrue(math.isnan(cf.unpack(a)[0]))

    def test_float_1_5(self):
        for nbits, exp_bits in self.float_sizes:
            cf = compile("f%d" % nbits)
            for endian in "little", "big":
                a = cf.pack(1.5, endian=endian)
                self.assertEqual(len(a), nbits)
                self.assertEqual(a.endian, endian)
                s = "0 0%s 1%s" % ((exp_bits - 1) * "1",
                                   (nbits - exp_bits - 2) * "0")
                if endian == "little":
                    s = s[::-1]
                self.assertEqual(a, bitarray(s))
                self.assertEqual(cf.unpack(a), (1.5, ))

    def test_float_errors(self):
        cf = compile("f16")
        self.assertRaises(struct.error, cf.pack, b"AB")
        self.assertRaises(ValueError, compile, "f8")

    def test_bits(self):
        cf = compile("b11")
        self.assertEqual(cf.width(), 11)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(bitarray("00001111 000", "big"), endian="little")
        self.assertEqual(a.endian, "little")
        self.assertEqual(a, bitarray("00001111 000"))
        self.assertEqual(cf.unpack(a), (bitarray("00001111 000"), ))
        self.assertRaises(TypeError, cf.pack, 12)
        self.assertRaises(ValueError, cf.pack, bitarray(10))

    def test_bytes(self):
        self.assertRaises(ValueError, compile, "B7")
        cf = compile("B24")
        self.assertEqual(cf.width(), 24)
        self.assertEqual(cf.values(), 1)
        a = cf.pack(b"ABC", endian="big")
        self.assertEqual(a.endian, "big")
        self.assertEqual(bytes(a), b"ABC")
        self.assertRaises(TypeError, cf.pack, 12)
        self.assertRaises(ValueError, cf.pack, b"AB")
        b = cf.unpack(a)[0]
        self.assertIs(type(b), bytes)
        self.assertEqual(cf.pack(bytearray(b"XYZ"), endian="little"),
                         bitarray(b"XYZ", "little"))

    def test_padding(self):
        cf = compile("x3 X2")
        self.assertEqual(cf.width(), 5)
        self.assertEqual(cf.values(), 0)
        a = cf.pack()
        self.assertEqual(a.to01(), "00011")
        res = cf.unpack(a)
        self.assertEqual(res, tuple())
        self.assertRaises(ValueError, cf.unpack, bitarray("10011"))
        self.assertRaises(ValueError, cf.unpack, bitarray("0001"))
        self.assertRaises(ValueError, compile("X2").unpack, bitarray("00"))


if __name__ == '__main__':
    unittest.main()
