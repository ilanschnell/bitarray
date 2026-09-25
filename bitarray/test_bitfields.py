# Copyright (c) 2026, Ilan Schnell; All Rights Reserved
# bitarray is published under the PSF license.
#
# Author: Ilan Schnell
"""
Tests for bitarray.bitfields module
"""
import math
import struct
import unittest
import dataclasses
from itertools import product

from bitarray import bitarray
from bitarray.bitfields import (Struct, compile, pack, unpack,
                                DEFAULT_ENDIAN, _ENDIAN_FROM_PREFIX)


class StructTests(unittest.TestCase):

    all_codes = "us?fhbBxX"

    def test_example1(self):
        fmt = "u3 s5 ? x2 B16 f32"
        values = (5, -3, True, b"AB", 1.5)
        a = pack(fmt, *values)
        self.assertEqual(unpack(fmt, a), values)

    def test_example2(self):
        cf = compile(">u2 s7 x3 X3 <u h4 b5 B16 f16")
        self.assertIsInstance(cf, Struct)
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
        self.assertIsNot(Struct("u8"), compile("u8"))

    def test_mixed_format_roundtrip(self):
        cf = compile("u3 >s5 <B16")
        self.assertEqual(compile(cf.format()), cf)
        self.assertEqual(Struct(cf.format()), cf)

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
        (">u12", 1,               int,      "000000000001"),
        ("<s9",  -13,             int,      "110011111"),
        ("<?1",  True,            bool,     "1"),
        ("<f16", -1.5,            float,    "0000000001 11110 1"),
        (">f16",  1.875,          float,    "0 01111 1110000000"),
        ("<h12", "af1",           str,      "0101 1111 1000"),
        (">h12", "2c3",           str,      "0010 1100 0011"),
        ("<b3",  bitarray("110"), bitarray, "110"),
        ("<B16", b"AC",           bytes,    "10000010 11000010"),
        (">B16", b"A ",           bytes,    "01000001 00100000"),
        ("<x3",  None,            None,     "000"),
        (">X5",  None,            None,     "11111"),
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
            self.assertEqual(a.endian, _ENDIAN_FROM_PREFIX[fmt[0]])
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
            for pre1, pre2 in product("<>", repeat=2):
                mixed_fmt = "%su3 %s %su3" % (pre1, fmt, pre2)
                values = [1, 3]
                if value is not None:
                    values.insert(1, value)
                a = pack(mixed_fmt, *values)
                self.assertEqual(a.endian, _ENDIAN_FROM_PREFIX[pre1])
                self.assertEqual(a[3:-3], bitarray(s))
                self.assertEqual(unpack(mixed_fmt, a), tuple(values))

    def test_mixed(self):
        for c, v in [("u8", 1),
                     ("s8", -10),
                     ("?", True),
                     ("f16", -2.0),
                     ("h4", "c"),
                     ("B8", b"A")]:
            cf = compile("<%s>%s" % (c, c))
            a = cf.pack(v, v)
            self.assertEqual(cf.unpack(a), (v, v))
            self.assertEqual(a[::-1], a)

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

    def test_struct_bytes(self):
        x = 1.875
        for nbits, struct_format in (16, "e"), (32, "f"), (64, "d"):
            for pre, endian in ("<", "little"), (">", "big"):
                bf = "%sf%d" % (pre, nbits)
                sf = pre + struct_format
                b = struct.pack(sf, x)
                self.assertEqual(bytes(pack(bf, x)), b)
                self.assertEqual(unpack(bf, bitarray(b, endian))[0], x)
                self.assertEqual(struct.unpack(sf, b)[0], x)

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
