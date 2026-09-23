"""
Pack and unpack bit-level structures using `bitarray`.

A format is a whitespace-separated sequence of fields.  Compile the format
once, then use the resulting `Struct` instance to pack values into a bitarray
or unpack a bitarray into a tuple::

    from bitarray import bitarray
    from bitfields import compile

    cf = compile("u3 s5 ? x2 b4 B16 f32")
    values = (5, -3, False, bitarray("1010"), b"AB", 1.5)
    a = cf.pack(*values)
    assert cf.unpack(a) == values

Padding fields do not consume or produce values.  The input to `unpack()`
must have exactly `cf.width` bits.  `cf.values` gives the number of
values consumed by `pack()` and returned by `unpack()`.

The supported field codes are:

u   An unsigned integer stored in the field width.
s   A signed integer stored in the field width using two's-complement
    representation.
?   A bool stored in field width one.  Packing uses normal Python truth-value
    testing.  Unpacking returns `bool`.
f   An IEEE floating-point value.  The width must be 16, 32, or 64.
b   A bitarray matching the field width.
B   A bytes or bytearray value occupying the field width.  The width must be
    a multiple of eight; unpacking always returns `bytes`.
x   Zero-padding bits.
X   One-padding bits.

The width *N* defaults to one when omitted.  The format
may be prefixed with `<` or `>`.  A leading `<` selects little-endian
bit and byte order; `>` selects big-endian. When omitted, `<` is assumed.
"""
import re
import struct
import functools
from dataclasses import dataclass

from bitarray import bitarray
from bitarray.util import int2ba, ba2int


__all__ = ["compile", "pack", "unpack"]


DEFAULT_ENDIAN = "little"


@dataclass(frozen=True)
class Field:

    width: int
    endian: str
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

    def pack(self, value):
        return int2ba(value, length=self.width, endian=self.endian,
                      signed=self.signed)

    def unpack(self, a):
        return ba2int(a, signed=self.signed)


@dataclass(frozen=True)
class BoolField(Field):

    code = "?"

    def __post_init__(self):
        if self.width != 1:
            raise ValueError("bool width must be 1")

    def pack(self, value):
        return bitarray("1" if value else "0")

    def unpack(self, a):
        return bool(a[0])


@dataclass(frozen=True)
class FloatField(Field):

    code = "f"
    formats = {16: "e", 32: "f", 64: "d"}

    def __post_init__(self):
        if self.width not in self.formats:
            raise ValueError("float must have width 16, 32 or 64, got %d" %
                             self.width)

    def struct_format(self):
        return (("<" if self.endian == "little" else ">") +
                self.formats[self.width])

    def pack(self, value):
        return bitarray(struct.pack(self.struct_format(), value),
                        endian=self.endian)

    def unpack(self, a):
        return struct.unpack(self.struct_format(), bytes(a))[0]


@dataclass(frozen=True)
class BitarrayField(Field):

    code = "b"

    def pack(self, a):
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

    def pack(self, value):
        if not isinstance(value, (bytes, bytearray)):
            raise TypeError("bytes expected, got %r" % type(value).__name__)
        if len(value) != self.width // 8:
            raise ValueError("bytes of length %d expected" %
                             (self.width // 8))
        return bitarray(value, endian=self.endian)

    def unpack(self, a):
        return bytes(a)


@dataclass(frozen=True)
class PaddingField(Field):

    value: bool
    consumes_value = False

    @property
    def code(self):
        return "X" if self.value else "x"

    def pack(self, value):
        return self.width * bitarray("1" if self.value else "0")

    def unpack(self, a):
        pass


@dataclass(frozen=True)
class Struct:

    fields: tuple
    width: int
    values: int
    endian: str
    pat = re.compile(r"([\w?])(\d*)")

    def __init__(self, format=""):
        if format.startswith(("<", ">")):
            endian = "little" if format[0] == "<" else "big"
            format = format[1:]
        else:
            endian = DEFAULT_ENDIAN
        object.__setattr__(self, "endian", endian)
        fields = []
        for s in format.split():
            match = self.pat.fullmatch(s)
            if match is None:
                raise ValueError("invalid format %r" % s)
            c = match.group(1)
            m = int(match.group(2) or 1)
            fields.append(self.field_from_code(c, m, endian))

        object.__setattr__(self, "fields", tuple(fields))
        object.__setattr__(self, "width", sum(f.width for f in fields))
        object.__setattr__(self, "values", sum(f.consumes_value
                                               for f in fields))


    @staticmethod
    def field_from_code(code, width, endian):
        if code in "us":
            return IntField(width, endian, signed=(code == "s"))
        if code == "?":
            return BoolField(width, endian)
        if code == "f":
            return FloatField(width, endian)
        if code == "b":
            return BitarrayField(width, endian)
        if code == "B":
            return BytesField(width, endian)
        if code in "xX":
            return PaddingField(width, endian, value=(code == "X"))
        raise ValueError("Not a valid code: %r" % code)

    def format(self):
        return ((">" if self.endian == "big" else "<") +
                " ".join(field.format() for field in self.fields))

    def pack(self, *values):
        if len(values) != self.values:
            raise ValueError("expected %d values to pack, got %d" %
                             (self.values, len(values)))
        a = bitarray(0, self.endian)
        i = 0  # value index
        for field in self.fields:
            value = None
            if field.consumes_value:
                value = values[i]
                i += 1
            a.extend(field.pack(value))
        return a

    def unpack(self, a):
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width:
            raise ValueError("expected bitarray of length %d, got %d" %
                             (self.width, len(a)))
        if a.endian != self.endian:
            raise ValueError("bit-endian mismatch")
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


@functools.lru_cache()
def compile(format):
    return Struct(format)

def pack(format, *values):
    cf = compile(format)
    return cf.pack(*values)

def unpack(format, a):
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected, got %r" % type(a).__name__)
    cf = compile(format)
    return cf.unpack(a)

# ---------------------------------------------------------------------------

import math
import unittest
import dataclasses


class StructTests(unittest.TestCase):

    def test_example1(self):
        fmt = "u3 s5 ? x2 b4 B16 f32"
        values = (5, -3, True, bitarray("1010"), b"AB", 1.5)
        a = pack(fmt, *values)
        self.assertEqual(unpack(fmt, a), values)

    def test_example2(self):
        cf = compile(">u2 x4 s7 x3 X3 u b5 B16 f16")
        self.assertEqual(cf.width, 57)
        self.assertEqual(cf.values, 6)
        self.assertEqual(cf.format(),
                         ">u2 x4 s7 x3 X3 u1 b5 B16 f16")
        values = 3, -2, 1, bitarray("01110"), b"A\xff", -29.0
        a = cf.pack(*values)
        self.assertEqual(len(a), 57)
        self.assertEqual(cf.unpack(a), values)

    def test_struct_read_only(self):
        cf = compile("u2 x s7 x X3 u b5")
        self.assertEqual(len(cf.fields), 7)
        self.assertEqual(cf.width, 20)
        self.assertEqual(cf.values, 4)
        for name in "endian", "fields", "width", "values":
            self.assertRaises(dataclasses.FrozenInstanceError,
                              setattr, cf, name, 0)

    def test_format(self):
        for format in "3x", "u8junk", "!", "q8", "1", "0z":
            self.assertRaises(ValueError, compile, format)
        self.assertEqual(compile("u2").format(), "<u2")
        self.assertEqual(compile(">u").format(), ">u1")

    def test_endian(self):
        for fmt, endian in [("<u4", "little"), (">u4", "big"),
                            ("u4", DEFAULT_ENDIAN)]:
            cf = compile(fmt)
            self.assertEqual(cf.endian, endian)
            self.assertEqual(compile(cf.format()), cf)
            value = 11
            self.assertEqual(cf.unpack(cf.pack(value)), (value,))

    def test_format_empty(self):
        cf = compile("")
        self.assertEqual(cf.endian, DEFAULT_ENDIAN)
        self.assertEqual(cf.width, 0)
        self.assertEqual(cf.values, 0)
        self.assertEqual(cf.pack(), bitarray())
        self.assertEqual(cf.unpack(bitarray(endian="little")), ())
        for fmt, endian in [("<", "little"), (">", "big"),
                            ("", DEFAULT_ENDIAN)]:
            cf = compile(fmt)
            self.assertEqual(cf.endian, endian)
            self.assertEqual(compile(cf.format()), cf)

    def test_pack_value_count(self):
        cf = compile("u8 s8")
        self.assertRaises(ValueError, cf.pack, 1)
        self.assertRaises(ValueError, cf.pack, 1, 2, 3)
        self.assertRaises(ValueError, compile("x").pack, 1)

    def test_unpack_errors(self):
        lst = [0, 1, 0, 0, 1, 1, 1, 1]
        cf = compile(">u8")
        self.assertRaises(ValueError, cf.unpack, bitarray(7))
        self.assertRaises(ValueError, cf.unpack, bitarray(9))
        self.assertRaises(TypeError, cf.unpack, lst)
        self.assertRaises(ValueError, cf.unpack, bitarray(8, "little"))
        # module level unpack
        self.assertRaises(TypeError, unpack, "u8", lst)
        self.assertRaises(ValueError, unpack, "u8", bitarray(8, "big"))


class FieldTests(unittest.TestCase):

    #    format  value            type      bitarray
    data = [
        ("<u11", 91,              int,      "11011010000"),
        ("<s9",  -13,             int,      "110011111"),
        ("<?1",  True,            bool,     "1"),
        ("<f16", -1.5,            float,    "0000000001 11110 1"),
        ("<b3",  bitarray("110"), bitarray, "110"),
        ("<B16", b"AC",           bytes,    "10000010 11000010"),
        ("<x3",  None,            None,     "000"),
        ("<X5",  None,            None,     "11111"),
    ]

    def test_compile(self):
        for fmt, value, tp, s in self.data:
            cf = compile(fmt)
            self.assertEqual(cf.width, len(bitarray(s)))
            self.assertEqual(cf.values, 0 if value is None else 1)
            self.assertEqual(cf.format(), fmt)

    def test_pack(self):
        for fmt, value, tp, s in self.data:
            if value is None:
                values = []
            else:
                self.assertIs(type(value), tp)
                values = [value]
            a = pack(fmt, *values)
            self.assertEqual(a.endian, "little")
            self.assertEqual(a, bitarray(s))

    def test_unpack(self):
        for fmt, value, tp, s in self.data:
            a = bitarray(s, "little")
            b = unpack(fmt, a)
            self.assertIs(type(b), tuple)
            if value is None:
                self.assertEqual(len(b), 0)
            else:
                self.assertEqual(len(b), 1)
                self.assertIs(type(b[0]), tp)
                self.assertEqual(b[0], value)

    def test_unsigned_int(self):
        cf = compile("<u20")
        self.assertEqual(cf.width, 20)
        self.assertEqual(cf.values, 1)
        a = cf.pack(1 << 19)
        self.assertEqual(a.endian, "little")
        self.assertEqual(a.to01(), "00000000000000000001")
        self.assertEqual(cf.unpack(a), (1 << 19, ))
        self.assertRaises(OverflowError, cf.pack, -1)
        self.assertRaises(OverflowError, cf.pack, 1 << 20)
        self.assertRaises(TypeError, cf.pack, 1.0)
        self.assertRaises(ValueError, compile, "u0")

    def test_signed_int(self):
        cf = compile("<s10")
        self.assertEqual(cf.width, 10)
        self.assertEqual(cf.values, 1)
        a = cf.pack(-1)
        self.assertEqual(a.endian, "little")
        self.assertEqual(a.to01(), "1111111111")
        self.assertEqual(cf.unpack(a), (-1, ))
        self.assertRaises(OverflowError, cf.pack, -513)
        self.assertRaises(OverflowError, cf.pack, 512)
        self.assertRaises(TypeError, cf.pack, -2.0)
        self.assertRaises(ValueError, compile, "s0")

    def test_bool(self):
        cf = compile("?")
        self.assertEqual(cf.endian, "little")
        self.assertEqual(cf.width, 1)
        self.assertEqual(cf.values, 1)
        for value in False, True, 0, 1, 2, "", "ya":
            a = cf.pack(value)
            self.assertEqual(a.endian, "little")
            self.assertEqual(len(a), 1)
            self.assertEqual(a[0], bool(value))
            v = cf.unpack(a)[0]
            self.assertEqual(type(v), bool)
            self.assertIs(v, bool(value))
        self.assertRaises(ValueError, compile, "?0")
        self.assertRaises(ValueError, compile, "?2")

    def test_float16(self):
        cf = compile("<f16")
        self.assertEqual(cf.width, 16)
        self.assertEqual(cf.values, 1)
        a = cf.pack(1.25)
        self.assertEqual(a.endian, "little")
        self.assertEqual(a, bitarray("0000000010 11110 0"))
        self.assertEqual(cf.unpack(a), (1.25, ))
        cf = compile(">f16")
        a = cf.pack(-3.0)
        self.assertEqual(a.endian, "big")
        self.assertEqual(a, bitarray("1 10000 1000000000"))
        self.assertEqual(cf.unpack(a), (-3.0, ))

    def test_float32(self):
        cf = compile(">f32")
        self.assertEqual(cf.width, 32)
        self.assertEqual(cf.values, 1)
        a = cf.pack(1.0)
        self.assertEqual(a, bitarray("0 01111111 00000000000000000000000"))

    def test_float64(self):
        cf = compile(">f64")
        self.assertEqual(cf.width, 64)
        self.assertEqual(cf.values, 1)
        a = cf.pack(0.0)
        self.assertEqual(a, 64 * bitarray("0"))

    # list of (nbits, exponent bits)
    float_sizes = [(16, 5), (32, 8), (64, 11)]

    def test_float_special(self):
        for nbits, exp_bits in self.float_sizes:
            cf = compile(">f%d" % nbits)
            self.assertEqual(cf.width, nbits)
            self.assertEqual(cf.values, 1)
            # -0.0
            a = cf.pack(-0.0)
            self.assertEqual(len(a), nbits)
            s = "1 %s" % ((nbits - 1) * "0")
            self.assertEqual(a, bitarray(s))
            x = cf.unpack(a)[0]
            self.assertEqual(x, 0.0)
            self.assertEqual(math.copysign(1.0, x), -1.0)
            # infinity
            a = cf.pack(float("inf"))
            self.assertEqual(len(a), nbits)
            s = "0 %s %s" % (exp_bits * "1", (nbits - exp_bits - 1) * "0")
            self.assertEqual(a, bitarray(s))
            self.assertEqual(cf.unpack(a), (float('inf'), ))
            # nan
            a = cf.pack(float("nan"))
            self.assertEqual(len(a), nbits)
            s = "0 %s 1%s" % (exp_bits * "1", (nbits - exp_bits - 2) * "0")
            self.assertEqual(a, bitarray(s))
            self.assertTrue(math.isnan(cf.unpack(a)[0]))

    def test_float_1_5(self):
        for nbits, exp_bits in self.float_sizes:
            for ef, endian in ("<", "little"), (">", "big"):
                cf = compile("%sf%d" % (ef, nbits))
                a = cf.pack(1.5)
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

    def test_bitarray(self):
        cf = compile("b11")
        self.assertEqual(cf.width, 11)
        self.assertEqual(cf.values, 1)
        s = "00001111 000"
        a = cf.pack(bitarray(s, "big"))
        self.assertEqual(a.endian, "little")
        self.assertEqual(a, bitarray(s))
        self.assertEqual(cf.unpack(a), (bitarray(s), ))
        self.assertRaises(TypeError, cf.pack, 12)
        self.assertRaises(ValueError, cf.pack, bitarray(10))

    def test_bytes(self):
        self.assertRaises(ValueError, compile, "B7")
        cf = compile(">B24")
        self.assertEqual(cf.width, 24)
        self.assertEqual(cf.values, 1)
        a = cf.pack(b"ABC")
        self.assertEqual(a.endian, "big")
        self.assertEqual(bytes(a), b"ABC")
        self.assertRaises(TypeError, cf.pack, 12)
        self.assertRaises(ValueError, cf.pack, b"AB")
        b = cf.unpack(a)[0]
        self.assertIs(type(b), bytes)
        self.assertEqual(cf.pack(bytearray(b"XYZ")), bitarray(b"XYZ", "big"))

    def test_padding(self):
        cf = compile("x3 X2")
        self.assertEqual(cf.width, 5)
        self.assertEqual(cf.values, 0)
        a = cf.pack()
        self.assertEqual(a.to01(), "00011")
        res = cf.unpack(a)
        self.assertEqual(res, tuple())
        self.assertRaises(ValueError, cf.unpack, bitarray("0001"))


if __name__ == '__main__':
    unittest.main()
