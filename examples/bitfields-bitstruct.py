"""
Compare `bitarray.bitfields` with the third-party `bitstruct` package.

Run this example with::

    python -m pip install bitstruct
    python examples/bitfields-bitstruct.py

Both modules compile format strings and pack fixed-width values at arbitrary
bit boundaries.  The tests below also document important differences:

* `bitstruct` packs to byte-padded `bytes`; `bitfields` packs to an
  exact-length `bitarray`.

* Their integer, float and padding codes overlap, but the codes for Boolean,
  raw-byte and bitarray fields differ.

* `bitstruct` has separate bit-order prefixes and a byte-order suffix.
  A `bitfields` prefix specifies the endianness of each field.

* `bitstruct` supports text, dictionaries, offsets and truncated input.
  `bitfields` supports named tuples, bitarrays, hexadecimal strings,
  comments and validating or explicit padding patterns.
"""
import unittest

import bitstruct  # type: ignore

from bitarray import bitarray, bitfields


class Similarities(unittest.TestCase):

    def test_common_fields(self):
        # u, s, f16, f32, f64, p and P have the same basic meaning.
        # bitstruct uses b for Boolean and r for raw bytes;
        # bitfields uses ? and B, respectively.
        struct_format = ">u3s5f16f32f64b1p3P2r16"
        fields_format = ">u3s5f16f32f64?1p3P2B16"
        values = (5, -3, 1.5, 1.875, -1.0, True, b"OK")

        packed_struct = bitstruct.pack(struct_format, *values)
        packed_fields = bitfields.pack(fields_format, *values)

        self.assertEqual(bitstruct.calcsize(struct_format), 142)
        self.assertEqual(len(packed_fields), 142)
        self.assertEqual(packed_struct, bytes(packed_fields))
        self.assertEqual(bitstruct.unpack(struct_format, packed_struct),
                         values)
        self.assertEqual(bitfields.unpack(fields_format, packed_fields),
                         values)

    def test_compiled_formats(self):
        struct_format = bitstruct.compile("u4s4")
        fields_format = bitfields.compile(">u4s4")

        packed_struct = struct_format.pack(10, -2)
        packed_fields = fields_format.pack(10, -2)
        self.assertEqual(packed_struct, bytes(packed_fields))
        self.assertEqual(struct_format.unpack(packed_struct), (10, -2))
        self.assertEqual(fields_format.unpack(packed_fields), (10, -2))


class Differences(unittest.TestCase):

    def test_output_and_byte_padding(self):
        packed_struct = bitstruct.pack("u3", 5)
        packed_fields = bitfields.pack(">u3", 5)

        self.assertIs(type(packed_struct), bytes)
        self.assertIs(type(packed_fields), bitarray)
        self.assertEqual(packed_struct, b"\xa0")
        self.assertEqual(packed_fields.to01(), "101")
        self.assertEqual(packed_fields.tobytes(), packed_struct)

        # bitstruct ignores the five byte-padding bits while bitfields
        # requires an input bitarray of exactly the compiled width.
        self.assertEqual(bitstruct.unpack("u3", packed_struct), (5, ))
        with self.assertRaises(ValueError):
            bitfields.unpack(">u3", bitarray(packed_struct, endian="big"))

    def test_format_defaults(self):
        # bitstruct requires every width and defaults to MSB-first order.
        # bitfields permits omitted widths and defaults to little endian.
        with self.assertRaises(bitstruct.Error):
            bitstruct.compile("u")
        self.assertEqual(bitfields.compile("u").format(), "<u1")

        self.assertEqual(bitstruct.pack("u3", 3), b"\x60")
        self.assertEqual(bitfields.pack("u3", 3).to01(), "110")
        self.assertEqual(bitfields.pack(">u3", 3).to01(), "011")

    def test_endianness_syntax(self):
        # The trailing < is bitstruct's little-endian byte-order suffix.
        # In bitfields, < prefixes a field and selects its bit and byte order.
        self.assertEqual(bitstruct.pack("u16<", 0x1234), b"\x34\x12")
        self.assertEqual(bitfields.pack("<u16", 0x1234).tobytes(), b"\x34\x12")

        # Prefixes persist for following fields in both format languages.
        self.assertEqual(bitstruct.pack(">u4u4", 4, 5), b"\x45")
        self.assertEqual(bitfields.pack(">u4u4", 4, 5).tobytes(), b"\x45")

    def test_different_field_codes(self):
        # Boolean fields.
        self.assertEqual(bitstruct.unpack("b1", bitstruct.pack("b1", True)),
                         (True, ))
        self.assertEqual(bitfields.unpack(">?", bitfields.pack(">?", True)),
                         (True, ))

        # bitstruct has text fields; bitfields has bitarray and hexadecimal
        # string fields.  Both can represent byte strings (r versus B).
        self.assertEqual(bitstruct.unpack("t16", bitstruct.pack("t16", "Hi")),
                         ("Hi", ))

        bits = bitarray("101")
        self.assertEqual(bitfields.unpack(">b3", bitfields.pack(">b3", bits)),
                         (bits, ))
        self.assertEqual(bitfields.unpack(">h8", bitfields.pack(">h8", "aF")),
                         ("af", ))

        raw = b"Hi"
        self.assertEqual(bitstruct.unpack("r16", bitstruct.pack("r16", raw)),
                         (raw, ))
        self.assertEqual(bitfields.unpack("B16", bitfields.pack("B16", raw)),
                         (raw, ))

    def test_padding(self):
        data = b"\xf5"

        # bitstruct padding is ignored during unpacking.
        self.assertEqual(bitstruct.unpack("p4u4", data), (5, ))

        # bitfields p/P padding is validated, while x/X is ignored.
        bits = bitarray(data, endian="big")
        with self.assertRaises(ValueError):
            bitfields.unpack(">p4 u4", bits)
        self.assertEqual(bitfields.unpack(">x4 u4", bits), (5, ))

        # Explicit padding patterns are specific to bitfields.
        cf = bitfields.compile(">p{101} u2")
        self.assertEqual(cf.pack(3).to01(), "10111")
        self.assertEqual(cf.unpack(bitarray("10111")), (3, ))

    def test_named_values(self):
        names = ["version", "length"]
        values = {"version": 4, "length": 5}

        # bitstruct keeps names outside its format and uses dictionaries.
        packed_struct = bitstruct.pack_dict("u4u4", names, values)
        self.assertEqual(bitstruct.unpack_dict("u4u4", names, packed_struct),
                         values)

        # bitfields embeds names and returns a named tuple.
        # Packing remains positional.
        cf = bitfields.compile(">u4{version} u4{length}")
        packed_fields = cf.pack(4, 5)
        fields = cf.unpack(packed_fields)
        self.assertEqual(packed_fields.tobytes(), packed_struct)
        self.assertEqual(fields, (4, 5))
        self.assertEqual((fields.version, fields.length), (4, 5))

    def test_offsets_and_truncated_input(self):
        # bitstruct directly supports offsets into byte buffers.
        buf = bytearray(2)
        bitstruct.pack_into("u4u4", buf, 4, 10, 11)
        self.assertEqual(buf, bytearray(b"\x0a\xb0"))
        self.assertEqual(bitstruct.unpack_from("u4u4", buf, 4), (10, 11))
        self.assertEqual(bitstruct.unpack("u8u8", b"\x12",
                                          allow_truncated=True),
                         (0x12, ))

        # With bitfields, normal bitarray slicing handles an offset, and
        # unpacking always requires exactly the compiled number of bits.
        bits = bitarray(buf, endian="big")
        self.assertEqual(bitfields.unpack(">u4 u4", bits[4:12]), (10, 11))
        with self.assertRaises(ValueError):
            bitfields.unpack(">u8 u8", bitarray(b"\x12", endian="big"))

    def test_bitfields_format_extensions(self):
        # Comments, embedded names and literal padding make larger bitfields
        # formats self-documenting.  bitstruct does not support comments.
        cf = bitfields.compile("""
            >h8{tag}       # hexadecimal tag
             b3{flags}     # exact bitarray value
             p{101}        # required marker bits
        """)
        values = ("af", bitarray("010"))
        packed = cf.pack(*values)
        self.assertEqual(packed.to01(), "10101111 010 101".replace(" ", ""))
        self.assertEqual(cf.unpack(packed), values)


if __name__ == "__main__":
    unittest.main()
