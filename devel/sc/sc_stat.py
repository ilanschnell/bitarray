import itertools
from bitarray import bitarray


def sc_stat(stream):
    """sc_stat(stream) -> dict

Scan a stream produced by `sc_encode()` and return a dictionary containing
its bit-endianness (`endian`), length (`nbits`), and the number of blocks
of each type (`blocks`).  `blocks` is a list such that `blocks[i]` is the
number of blocks of type `i`.

Except for returning statistics instead of a bitarray, this function
behaves like `sc_decode()`.
"""
    def decode_header(stream):
        head = next(stream)
        if head & 0xe0:
            raise ValueError("invalid header: 0x%02x" % head)
        endian = 'big' if head & 0x10 else 'little'
        length = head & 0x0f  # number of bytes representing nbits
        nbits = 0
        for j in range(length):
            nbits |= next(stream) << 8 * j
        return dict(endian=endian, nbits=nbits)

    def scan_block(stream, stats):
        head = next(stream)

        if head < 0xa0:                          # type 0 - 0x00 -- 0x9f
            if head == 0:  # stop byte
                return False
            n = 0
            k = head if head <= 32 else 32 * (head - 31)
        elif head < 0xc0:                        # type 1 - 0xa0 .. 0xbf
            n = 1
            k = head - 0xa0
        elif 0xc2 <= head <= 0xc4:               # type 2 .. 4 - 0xc2 .. 0xc4
            n = head - 0xc0
            k = next(stream)                     # index count byte
        else:
            raise ValueError("Invalid block head: 0x%02x" % head)

        stats['blocks'][n] += 1

        nc = max(1, n) * k   # size of block data to consume below
        next(itertools.islice(stream, nc, nc), None)
        return True

    stream = iter(stream)
    stats = decode_header(stream)
    stats['blocks'] = 5 * [0]

    while scan_block(stream, stats):
        pass

    return stats

# ---------------------------------------------------------------------------

import unittest

from bitarray.util import sc_encode, sc_decode, urandom
from bitarray.test_bitarray import ENDIANS


class Tests(unittest.TestCase):

    def test_empty(self):
        blob = b"\x01\x00\0"
        self.assertEqual(sc_stat(blob), {'endian': 'little',
                                         'nbits': 0,
                                         'blocks': [0, 0, 0, 0, 0]})
        self.assertEqual(sc_decode(blob), bitarray())

    def test_zeros_explicit(self):
        for blob, blocks in [
                (b"\x11\x08\0",         [0, 0, 0, 0, 0]),
                (b"\x11\x08\x01\x00\0", [1, 0, 0, 0, 0]),
                (b"\x11\x08\xa0\0",     [0, 1, 0, 0, 0]),
                (b"\x11\x08\xc2\x00\0", [0, 0, 1, 0, 0]),
                (b"\x11\x08\xc3\x00\0", [0, 0, 0, 1, 0]),
                (b"\x11\x08\xc4\x00\0", [0, 0, 0, 0, 1]),
        ]:
            stat = sc_stat(blob)
            self.assertEqual(stat, {'endian': 'big',
                                    'nbits': 8,
                                    'blocks': blocks})
            a = sc_decode(blob)
            self.assertEqual(a.endian, 'big')
            self.assertEqual(len(a), 8)
            self.assertFalse(a.any())

    def test_untouch(self):
        blob = b"\x01\x07\x01\x73\0XYZ"
        stream = iter(blob)
        stat = sc_stat(stream)
        self.assertEqual(stat, {'endian': 'little',
                                'nbits': 7,
                                'blocks': [1, 0, 0, 0, 0]})
        self.assertEqual(next(stream), ord('X'))
        stream = iter(blob)
        self.assertEqual(sc_decode(stream), bitarray("1100111"))
        self.assertEqual(next(stream), ord('X'))

    def test_endian(self):
        for endian in ENDIANS:
            a = urandom(400, endian)
            blob = sc_encode(a)
            stat = sc_stat(blob)
            self.assertEqual(stat, {'nbits': len(a),
                                    'endian': endian,
                                    'blocks': [2, 0, 0, 0, 0]})

    def test_stop_iteration(self):
        for blob in [b'', b'\x00', b'\x01', b'\x02\x77',
                     b'\x01\x04\x01', b'\x01\x04\xa1', b'\x01\x04\xa0']:
            self.assertRaises(StopIteration, sc_stat, blob)
            self.assertRaises(StopIteration, sc_decode, blob)

    def test_values(self):
        b = [0x11, 3, 1, 32, 0]
        self.assertEqual(sc_decode(b), bitarray("001"))
        self.assertEqual(sc_stat(b), {'endian': 'big',
                                      'nbits': 3,
                                      'blocks': [1, 0, 0, 0, 0]})
        for x in -1, 256:
            b[-1] = x
            self.assertRaises(ValueError, sc_stat, b)
        for x in None, "F", Ellipsis, []:
            b[-1] = x
            self.assertRaises(TypeError, sc_stat, b)

    def test_example(self):
        n = 1 << 26
        a = bitarray(n, 'little')
        a[:1 << 16] = 1
        for i in range(2, 1 << 16):
            a[n // i] = 1
        b = sc_encode(a)
        stat = sc_stat(b)
        self.assertEqual(stat, {'endian': 'little',
                                'nbits': n,
                                'blocks': [2, 147, 3, 1, 1]})
        self.assertEqual(a, sc_decode(b))

        a.reverse()
        b = sc_encode(a)
        self.assertEqual(sc_stat(b)['blocks'], [2, 256, 254, 3, 0])
        self.assertEqual(a, sc_decode(b))


if __name__ == '__main__':
    unittest.main()
