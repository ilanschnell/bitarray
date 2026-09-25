"""
Pack and unpack bit-level structures using `bitarray`.

A format is a sequence of fields; whitespace between fields is optional.
Compile the format once, then use the resulting `Struct` instance to pack
values into a bitarray or unpack a bitarray into a tuple::

    from bitfields import compile

    cf = compile("u3 s5 ? x2 h12 B16 f32")
    values = (5, -3, False, "1fa", b"AB", 1.5)
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
h   A hexadecimal string occupying the field width.  The width must be a
    multiple of four; packing is case-insensitive and ignores whitespace;
    unpacking returns lowercase.
b   A bitarray matching the field width.
B   A bytes or bytearray value occupying the field width.  The width must be
    a multiple of eight; unpacking always returns `bytes`.
x   Zero-padding bits.
X   One-padding bits.

The width *N* defaults to one when omitted.  Each field may be prefixed
with `<` for little-endian or `>` for big-endian bit and byte order.  The
selected order applies to subsequent fields until changed; initially it is
little-endian.
"""
import re
import struct
import functools
from dataclasses import dataclass
from typing import Any, Tuple

from bitarray import bitarray
from bitarray.util import int2ba, ba2int, hex2ba, ba2hex


__all__ = ["Struct", "compile", "pack", "unpack"]


DEFAULT_ENDIAN = "little"

_ENDIAN_FROM_PREFIX = {"<": "little", ">": "big"}
_PREFIX_FROM_ENDIAN = {v: k for k, v in _ENDIAN_FROM_PREFIX.items()}


@dataclass(frozen=True)
class _Field:

    width: int
    endian: str
    consumes_value = True
    code = ""

    def format(self):
        return "%s%s%d" % (_PREFIX_FROM_ENDIAN[self.endian],
                           self.code, self.width)


@dataclass(frozen=True)
class _IntField(_Field):

    signed: bool

    @property
    def code(self):
        return "s" if self.signed else "u"

    def pack(self, value):
        return int2ba(value, length=self.width, endian=self.endian,
                      signed=self.signed)

    def unpack(self, a):
        return ba2int(a, signed=self.signed)


@dataclass(frozen=True)
class _BoolField(_Field):

    code = "?"

    def __post_init__(self):
        if self.width != 1:
            raise ValueError("bool width must be 1")

    def pack(self, value):
        return bitarray("1" if value else "0")

    def unpack(self, a):
        return bool(a[0])


@dataclass(frozen=True)
class _FloatField(_Field):

    code = "f"
    formats = {16: "e", 32: "f", 64: "d"}

    def __post_init__(self):
        if self.width not in self.formats:
            raise ValueError("float must have width 16, 32 or 64, got %d" %
                             self.width)

    def struct_format(self):
        return _PREFIX_FROM_ENDIAN[self.endian] + self.formats[self.width]

    def pack(self, value):
        return bitarray(struct.pack(self.struct_format(), value),
                        endian=self.endian)

    def unpack(self, a):
        return struct.unpack(self.struct_format(), bytes(a))[0]


@dataclass(frozen=True)
class _HexField(_Field):

    code = "h"

    def __post_init__(self):
        if self.width % 4:
            raise ValueError("width not a multiple of 4")

    def pack(self, value):
        if not isinstance(value, str):
            raise TypeError("str expected, got %r" % type(value).__name__)
        a = hex2ba(value, self.endian)
        if len(a) != self.width:
            raise ValueError("hex string with %d digits expected" %
                             (self.width // 4))
        return a

    def unpack(self, a):
        return ba2hex(a)


@dataclass(frozen=True)
class _BitarrayField(_Field):

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
class _BytesField(_Field):

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
class _PaddingField(_Field):

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
    "Struct(format) -> compiled struct object"

    _fields: tuple
    width: int
    values: int

    def __init__(self, format: str = "") -> None:
        fields = self._fields_from_format(format)
        object.__setattr__(self, "_fields", tuple(fields))
        object.__setattr__(self, "width", sum(f.width for f in fields))
        object.__setattr__(self, "values", sum(f.consumes_value
                                               for f in fields))

    pat = re.compile(r"""
        ([<>])?   # optional prefix; < or >
        ([\w?])   # code character
        (\d*)     # optional bit width; defaults to 1
        \s*       # optional whitespace
    """, re.VERBOSE)

    def _fields_from_format(self, format):
        endian = DEFAULT_ENDIAN
        fields = []
        format = format.lstrip()
        while format:
            m = self.pat.match(format)
            if m is None:
                raise ValueError("invalid format: %r" % format)
            pre = m.group(1)
            if pre is not None:
                endian = _ENDIAN_FROM_PREFIX[pre]
            c = m.group(2)
            w = int(m.group(3) or 1)
            if w == 0:
                raise ValueError("field width cannot be zero: %r" % format)
            fields.append(self._field_from_code(c, w, endian))
            format = format[m.end():]

        return fields

    @staticmethod
    def _field_from_code(code, width, endian):
        if code in "us":
            return _IntField(width, endian, signed=(code == "s"))
        if code == "?":
            return _BoolField(width, endian)
        if code == "f":
            return _FloatField(width, endian)
        if code == "h":
            return _HexField(width, endian)
        if code == "b":
            return _BitarrayField(width, endian)
        if code == "B":
            return _BytesField(width, endian)
        if code in "xX":
            return _PaddingField(width, endian, value=(code == "X"))
        raise ValueError("Not a valid code: %r" % code)

    def format(self) -> str:
        """format() -> str

Return the canonical format string reconstructed from this compiled format.
"""
        return " ".join(field.format() for field in self._fields)

    def __repr__(self):
        return "Struct(%r)" % self.format()

    def pack(self, *values: Any) -> bitarray:
        """pack(v1, v2, ...) -> bitarray

Return a bitarray containing the values v1, v2, ... packed according to this
compiled format.
"""
        if len(values) != self.values:
            raise ValueError("expected %d values to pack, got %d" %
                             (self.values, len(values)))
        fields = self._fields
        a = bitarray(0, fields[0].endian if fields else DEFAULT_ENDIAN)
        i = 0  # value index
        for field in fields:
            value = None
            if field.consumes_value:
                value = values[i]
                i += 1
            a.extend(field.pack(value))
        return a

    def unpack(self, a: bitarray) -> Tuple[Any, ...]:
        """unpack(bitarray) -> tuple

Return a tuple containing values unpacked according to this compiled format.
"""
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width:
            raise ValueError("expected bitarray of length %d, got %d" %
                             (self.width, len(a)))
        i = 0
        res = []
        for field in self._fields:
            j = i + field.width
            b = bitarray(a[i:j], field.endian)
            value = field.unpack(b)
            if field.consumes_value:
                res.append(value)
            i = j
        return tuple(res)


@functools.lru_cache()
def compile(format: str) -> Struct:
    """compile(format) -> Struct

Compile given format string and return a compiled format object that
can be used to pack and/or unpack data multiple times.
"""
    return Struct(format)

def pack(format: str, *values: Any) -> bitarray:
    """pack(format, v1, v2, ...) -> bitarray

Return a bitarray containing the values v1, v2, ... packed according
to the format string.
"""
    cf = compile(format)
    return cf.pack(*values)

def unpack(format: str, a: bitarray) -> Tuple[Any, ...]:
    """unpack(format, bitarray) -> tuple

Return a tuple containing values unpacked according to the format string.
"""
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected, got %r" % type(a).__name__)
    cf = compile(format)
    return cf.unpack(a)

# ---------------------------------------------------------------------------

import math
import unittest
import dataclasses


class StructTests(unittest.TestCase):

    all_codes = "us?fhbBxX"

    def test_example1(self):
        fmt = "u3 s5 ? x2 B16 f32"
        values = (5, -3, True, b"AB", 1.5)
        a = pack(fmt, *values)
        self.assertEqual(unpack(fmt, a), values)

    def test_example2(self):
        cf = compile(">u2 s7 x3 X3 <u h4 b5 B16 f16")
        self.assertEqual(cf.width, 57)
        self.assertEqual(cf.values, 7)
        self.assertEqual(cf.format(),
                         ">u2 >s7 >x3 >X3 <u1 <h4 <b5 <B16 <f16")
        values = 2, -8, 1, "e", bitarray("01110"), b"A\xff", -29.0
        a = cf.pack(*values)
        self.assertEqual(len(a), 57)
        self.assertEqual(a.endian, "big")
        self.assertEqual(a, bitarray("10 1111000 000 111 1 0111 01110 "
                                     "10000010 11111111 0000001011110011"))
        self.assertEqual(cf.unpack(a), values)

    def test_cached(self):
        self.assertIs(compile("u8"), compile("u8"))

    def test_mixed_format_roundtrip(self):
        cf = compile("u3 >s5 <B16")
        self.assertEqual(compile(cf.format()), cf)

    def test_struct_read_only(self):
        cf = compile("u2 x s7 x X3 u b5")
        self.assertEqual(len(cf._fields), 7)
        self.assertEqual(cf.width, 20)
        self.assertEqual(cf.values, 4)
        for name in "_fields", "width", "values":
            self.assertRaises(dataclasses.FrozenInstanceError,
                              setattr, cf, name, 0)

    def test_zero_width(self):
        for c in self.all_codes:
            # zero width is consistently rejected
            self.assertRaises(ValueError, compile, c + "0")

    def test_default_width(self):
        for c in self.all_codes:
            if c in "fhB":
                self.assertRaises(ValueError, compile, c)
                continue
            cf = compile(c)
            self.assertEqual(cf.width, 1)
            self.assertEqual(cf.format(), "<%s1" % c)

    def test_format(self):
        cf = compile("u3 s5 >? x2 <b4 B16 f32")
        self.assertEqual(cf.format(), "<u3 <s5 >?1 >x2 <b4 <B16 <f32")

        for fmt in ">u3 <x1 b4", ">u3<xb4", ">u3<x<b4", " >u3 <x b4 ":
            cf = compile(fmt)
            self.assertEqual(cf.format(), ">u3 <x1 <b4")
            self.assertEqual(cf.pack(3, bitarray("0110")).endian, "big")

        for format in "<", "<<u8", "3x", "u8junk", "!", "q8", "1", ">0z":
            self.assertRaises(ValueError, compile, format)
        self.assertEqual(compile("u2").format(), "<u2")
        self.assertEqual(compile(">u").format(), ">u1")

    def test_format_empty(self):
        for fmt in "", "  ":
            cf = compile(fmt)
            self.assertEqual(cf.width, 0)
            self.assertEqual(cf.values, 0)
            self.assertEqual(cf.unpack(bitarray(endian="big")), ())
            a = cf.pack()
            self.assertEqual(len(a), 0)
            self.assertEqual(a.endian, DEFAULT_ENDIAN)

    def test_repr(self):
        self.assertEqual(repr(compile("u2 >s7 x")), "Struct('<u2 >s7 >x1')")

    def test_endian(self):
        for fmt, endian in [("<u4", "little"), (">u4", "big"),
                            ("u4", DEFAULT_ENDIAN)]:
            cf = compile(fmt)
            a = cf.pack(11)
            self.assertEqual(len(a), 4)
            self.assertEqual(a.endian, endian)
            self.assertEqual(compile(cf.format()), cf)
            value = 11
            self.assertEqual(cf.unpack(cf.pack(value)), (value,))

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
        # module level unpack
        self.assertRaises(TypeError, unpack, "u8", lst)


class FieldTests(unittest.TestCase):

    #    format  value            type      bitarray
    data = [
        ("<u11", 91,              int,      "11011010000"),
        ("<s9",  -13,             int,      "110011111"),
        ("<?1",  True,            bool,     "1"),
        ("<f16", -1.5,            float,    "0000000001 11110 1"),
        ("<h12", "af1",           str,      "0101 1111 1000"),
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
            values = []
            if value is not None:
                self.assertIs(type(value), tp)
                values.append(value)
            a = pack(fmt, *values)
            self.assertIs(type(a), bitarray)
            self.assertEqual(a.endian, "little")
            self.assertEqual(a, bitarray(s))

    def test_unpack(self):
        for fmt, value, tp, s in self.data:
            for endian in "little", "big":
                a = bitarray(s, endian)
                b = unpack(fmt, a)
                self.assertIs(type(b), tuple)
                if value is None:
                    self.assertEqual(len(b), 0)
                else:
                    self.assertEqual(len(b), 1)
                    self.assertIs(type(b[0]), tp)
                    self.assertEqual(b[0], value)

    def test_roundtrip(self):
        for fmt, value, tp, s in self.data:
            fmt = ">u3%s>u3" % fmt
            values = [1, 3]
            if value is not None:
                values.insert(1, value)
            a = pack(fmt, *values)
            self.assertEqual(a.endian, "big")
            self.assertEqual(a, bitarray("001" + s + "011"))
            self.assertEqual(unpack(fmt, a), tuple(values))

    def test_mixed(self):
        for c, v1, v2 in [("u8", 1, 2),
                          ("s8", -10, -9),
                          ("?", False, True),
                          ("f16", 1.0, -2.0),
                          ("h8", "a1", "f0"),
                          ("B16", b"AB", b"CD")]:
            cf = compile("<%s>%s" % (c, c))
            self.assertEqual(cf.unpack(cf.pack(v1, v2)), (v1, v2))

        cf = compile("<b4>b4")
        a = bitarray("0110", "big")
        b = bitarray("1100", "little")
        packed = cf.pack(a, b)
        self.assertEqual(packed.endian, "little")
        self.assertEqual(packed, a + b)
        x, y = cf.unpack(packed)
        self.assertEqual((x, y), (a, b))
        self.assertEqual((x.endian, y.endian), ("little", "big"))

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

    def test_bool(self):
        cf = compile("?")
        self.assertEqual(cf.pack(True).endian, "little")
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
        self.assertRaises(ValueError, compile, "?2")

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

    def test_hex(self):
        self.assertRaises(ValueError, compile, "h7")
        cf = compile("<h20")
        self.assertEqual(cf.width, 20)
        self.assertEqual(cf.values, 1)
        a = cf.pack("1fA73")
        self.assertEqual(cf.pack("1f a73"), a)  # whitespace is ignored
        self.assertEqual(a.endian, "little")
        self.assertEqual(a, bitarray("1000 1111 0101 1110 1100"))
        self.assertEqual(cf.unpack(a), ("1fa73", ))
        self.assertRaises(TypeError, cf.pack, b"1fa73")
        self.assertRaises(ValueError, cf.pack, "1fa7")
        self.assertRaises(ValueError, cf.unpack, bitarray(19))

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
        # padding bits are ignored rather than validated
        self.assertEqual(unpack("x3 X3", bitarray("111000")), ())


if __name__ == '__main__':
    unittest.main()
