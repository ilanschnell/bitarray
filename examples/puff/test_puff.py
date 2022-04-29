import os
import zlib
import unittest

from bitarray import bitarray

from puff import State, Puff


class TestState(unittest.TestCase):

    def test_simple(self):
        a = bitarray(80)
        b = bytearray()
        s = State(a, b)
        self.assertEqual(s.get_incnt(), 0)
        self.assertEqual(len(b), 0)
        s.extend_block(4)
        self.assertEqual(s.get_incnt(), 32)
        self.assertEqual(len(b), 4)
        a[32:35] = bitarray('011')
        self.assertEqual(s.read_uint(3), 6)
        self.assertEqual(s.get_incnt(), 35)

    def test_read_uint(self):
        # works for either bit endianness
        inp = bitarray('11011100 1')
        out = bytearray()
        s = State(inp, out)
        self.assertRaises(ValueError, s.read_uint, -1)  # negative bits
        self.assertEqual(s.read_uint(0), 0)  # reading zero bits is OK
        self.assertEqual(s.read_uint(3), 3)
        self.assertEqual(s.read_uint(5), 7)
        self.assertEqual(s.get_incnt(), 8)
        self.assertRaises(ValueError, s.read_uint, 2)  # end of input
        self.assertEqual(s.read_uint(1), 1)
        self.assertEqual(s.read_uint(0), 0)
        self.assertEqual(s.get_incnt(), 9)
        self.assertRaises(ValueError, s.read_uint, 1)  # end of input
        self.assertEqual(len(out), 0)                  # nothing in output

    def test_read_uint32(self):
        a = bitarray(endian='little')
        a.frombytes(b'\x7e\xae\xd4\xbb')
        s = State(a, bytearray())
        self.assertEqual(s.read_uint(32), 0xbbd4ae7e)
        self.assertEqual(s.get_incnt(), 32)

        a = bitarray(32 * '1', endian='little')
        s = State(a, bytearray())
        self.assertEqual(s.read_uint(32), (1 << 32) - 1)
        self.assertEqual(s.get_incnt(), 32)

    def test_copy(self):
        a = bitarray()  # nothing is read from input in this test
        out = bytearray(b'ABC')
        s = State(a, out)
        s.copy(3, 2)
        self.assertEqual(bytes(out), b'ABCAB')
        self.assertRaises(ValueError, s.copy, 6, 1)   # distance too far back
        s.copy(5, 10)
        s.copy(6, 0)  # does nothing (length is zero)
        self.assertEqual(bytes(out), b'ABCABABCABABCAB')
        self.assertRaises(ValueError, s.copy, 0, 1)   # distance zero
        self.assertRaises(ValueError, s.copy, -1, 1)  # distance negative
        self.assertRaises(ValueError, s.copy, 16, 1)  # distance too far back
        self.assertRaises(ValueError, s.copy, 1, -1)  # length negative

    def test_append_byte(self):
        out = bytearray()
        s = State(bitarray(), out)
        s.append_byte(0)
        self.assertRaises(ValueError, s.append_byte, -1)
        self.assertRaises(ValueError, s.append_byte, 256)
        s.append_byte(255)
        self.assertEqual(bytes(out), b'\0\xff')

    def test_extend_block(self):
        a = bitarray()
        a.frombytes(b'ABCDEF')
        b = bytearray()
        s = State(a, b)

        s.extend_block(0)
        self.assertEqual(bytes(b), b'')

        s.extend_block(2)
        self.assertEqual(bytes(b), b'AB')
        self.assertRaises(ValueError, s.extend_block, 5)  # not enough input

        s.extend_block(1)
        self.assertEqual(bytes(b), b'ABC')
        self.assertEqual(s.get_incnt(), 24)

        s.read_uint(3)
        self.assertRaises(ValueError, s.extend_block, 1)  # input unaligned
        s.read_uint(5)
        s.extend_block(2)
        self.assertEqual(bytes(b), b'ABCEF')  # we skipped 'D'
        # invalid block size
        self.assertRaises(ValueError, s.extend_block, -1)
        self.assertRaises(ValueError, s.extend_block, 0x10000)

    def test_decode_lengths(self):
        # this is taken from the stream of dynamic header bits - after nlen,
        # ndist, ncode and the (up to 19) code length code lengths are read
        a = bitarray('''
    11001100 00001100 00011101 11011101 11000011 00000111 00001000 00101100
    00011100 10000011 01100000 11101110 11101011 00000111 01011000 00111011
    10111100 00000010 00011000 00111011 10111010 01000010 00001110 11100001
    00000111 01110000 10010010 00001001 10000011 10110100 00100001 10000011
    10111011 10101100 00011101 11011100 00100111 10111111 11011001 11011110
    11111011 01010111 10110100 11111010 11101010 11101010 10101110 11110100
    01011110 10001110 01010101 11010101 01011111 11010111 1100
        ''')
        ncode = 279 + 23

        b = bytearray()
        s = State(a, b)
        length = s.decode_lengths([4, 0, 6, 5, 4, 0, 0, 4, 2, 3,
                                   6, 0, 5, 5, 0, 0, 2, 4, 0], ncode)
        # no bytes were added to the output stream
        self.assertEqual(len(b), 0)
        # the code lengths list contains literal/lengths and distance codes
        self.assertEqual(len(length), ncode)
        # we've exhausted the input array exactly
        self.assertEqual(s.get_incnt(), len(a))
        # simple sum check, as I didn't want to cut and paste the whole list
        self.assertEqual(sum(length), 2183)

    def test_decode_lengths_error(self):
        a = bitarray(1000)
        b = bytearray()
        s = State(a, b)
        # nlen > 316 (MAXCODES)
        self.assertRaises(ValueError, s.decode_lengths, 19 * [0], 317)
        # sequence length not 19
        self.assertRaises(ValueError, s.decode_lengths, 20 * [0], 316)

    def test_decode_block_error(self):
        a = bitarray(1000)
        b = bytearray()
        s = State(a, b)
        # nlen > 288 (FIXLCODES)
        self.assertRaises(ValueError, s.decode_block, 273 * [0], 289, 23)
        # ndist > 32 (FIXDCODES)
        self.assertRaises(ValueError, s.decode_block, 274 * [0], 279, 33)
        # sequence length not 279 + 23 = 302
        self.assertRaises(ValueError, s.decode_block, 301 * [0], 279, 23)


class TestPuff(unittest.TestCase):

    def test_constants(self):
        from puff import FIXLCODES, FIXDCODES, FIXED_LENGTHS
        self.assertEqual(len(FIXED_LENGTHS), FIXLCODES + FIXDCODES)

    def test_align_byte_boundary(self):
        a = bitarray(15)
        d = Puff(a, bytearray())
        d.read_uint(5)
        d.align_byte_boundary()
        self.assertEqual(d.get_incnt(), 8)
        d.align_byte_boundary()
        self.assertEqual(d.get_incnt(), 8)
        d.read_uint(1)
        self.assertRaises(ValueError, d.align_byte_boundary)

    def round_trip(self, data, level=-1):
        compressed = zlib.compress(data, level=level)

        a = bitarray(0, 'little')
        a.frombytes(compressed)
        out = bytearray()
        p = Puff(a, out)
        # check zlib header
        self.assertEqual(p.read_uint(8), 0x78)
        self.assertTrue(p.read_uint(8) in (0x01,   # no compression
                                           0x5e,   # low compression
                                           0x9c,   # default compression
                                           0xda))  # best compression
        p.process_blocks()

        self.assertEqual(bytes(out), data)

    def test_zeros(self):
        for n in 0, 1, 10, 100, 1000, 10_000:
            self.round_trip(n * b'\0')

    def test_this_file(self):
        with open(__file__, 'rb') as f:
            data = f.read()
        for level in range(1, 10):
            self.round_trip(data, level)

    def test_words(self):
        with open('/usr/share/dict/words', 'rb') as f:
            data = f.read()
        self.round_trip(data)

    def test_random_data(self):
        self.round_trip(os.urandom(2000))


if __name__ == '__main__':
    unittest.main()
