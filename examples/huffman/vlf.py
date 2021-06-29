"""
Variable length bitarray format
===============================

The variable length format implemented in this module is similar to LEB128.
It is used to store arbitrarily large bitarrays in a small number of bytes.
A single byte can store bitarrays up to 4 element, every additional byte
stores up to 7 more elements.

The most significant bit of each byte indicated whether more bytes follow.
In addition, the first byte contains 3 bits which indicate the number of
padding bits at the end of the stream.  Here is an example:

     010101001110011          raw bitarray (length 15)
     0101  0100111  0011      grouped (4, 7, 7, ...)
     0101  0100111  0011000   pad last group with zeros
  0110101  0100111  0011000   add number of pad bits (3) add to front (011)
 10110101 10100111 00011000   add high bits
     0xb5     0xa7     0x18   in hexadecimal - output stream
"""
from bitarray import bitarray, get_default_endian


def decode(stream, endian=None):
    a = bitarray(0, 'big')
    b = next(stream)
    unused = (b & 0x70) >> 4
    if unused >= 7 or (b & 0x80 == 0 and unused > 4):
        raise ValueError("Invalid header byte: 0x%02x" % b)
    a.frombytes(bytes([b]))
    del a[:4]
    while b & 0x80:
        b = next(stream)
        a.frombytes(bytes([b]))
        del a[-8:-7]
    if unused:
        del a[-unused:]
    if endian is None:
        endian = get_default_endian()
    return a if endian == 'big' else bitarray(a, 'little')


def encode(a):
    n = (len(a) + 9) // 7  # number of resulting bytes
    m = 7 * n - 3          # number of bits resulting bytes can hold
    unused = m - len(a)    # number of pad bits
    assert 0 <= unused < 7
    res = bitarray(0, 'big')
    res.append(len(a) > 4)      # leading bit
    for x in 4, 2, 1:           # encode number of pad bits
        res.append(bool(x & unused))
    res.extend(a[:4])
    for i in range(4, len(a), 7):
        res.append(i + 7 < m)   # leading bit
        res.extend(a[i:i + 7])
    return res.tobytes()

# ---------------------------------------------------------------------------

from random import randint
import unittest

from bitarray.util import urandom


class VLFTests(unittest.TestCase):

    def test_explicit(self):
        for s, bits in [
                (b'\x40', ''),
                (b'\x30', '0'),
                (b'\x38', '1'),
                (b'\x00', '0000'),
                (b'\x01', '0001'),
                (b'\xe0\x40', '0000 1'),
                (b'\x90\x02', '0000 000001'),
                (b'\xb5\xa7\x18', '0101 0100111 0011'),  # module docstring
                (b'\xe8\x80\x84\x40', '1000 0000000 0000100 1'),
                (b'\x80\x80\x80\x80\x00', (4 + 7 * 4) * '0'),
        ]:
            a = bitarray(bits)
            self.assertEqual(encode(a), s)
            self.assertEqual(decode(iter(s)), a)

    def test_ambiguity(self):
        for s in b'\x40', b'\x4f', b'\x45\xff':
            self.assertEqual(decode(iter(s)), bitarray())
        for s in b'\x1e', b'\x1f':
            self.assertEqual(decode(iter(s)), bitarray('111'))

    def test_multiple(self):
        stream = iter(b'\x30\x38\x40\x2c\xe0\x40')
        for bits in '0', '1', '', '11', '00001':
            self.assertEqual(decode(stream), bitarray(bits))

        arrays = [urandom(randint(0, 30)) for _ in range(1000)]
        stream = iter(b''.join(encode(a) for a in arrays))
        for a in arrays:
            self.assertEqual(decode(stream), a)

    def test_decode_errors(self):
        # invalid number of padding bits
        self.assertRaises(ValueError, decode, iter(b'\xf0'))
        for s in b'\x70', b'\x60', b'\x50':
            self.assertRaises(ValueError, decode, iter(s))
        # high bit set, but no continuation
        for s in b'\x80', b'\x80\x80':
            self.assertRaises(StopIteration, decode, iter(s))

    def test_random(self):
        for n in range(500):
            a = urandom(n, endian='big')
            s = encode(a)
            self.assertEqual(len(s), (len(a) + 3 + 6) // 7)
            self.assertEqual(s, encode(bitarray(a, 'little')))

            for endian in 'big', 'little':
                b = decode(iter(s), endian)
                self.assertEqual(b, a)
                self.assertEqual(b.endian(), endian)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
