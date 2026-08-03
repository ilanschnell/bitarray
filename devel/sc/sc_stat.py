from itertools import islice
from bitarray import bitarray


def sc_decode_header(stream):
    head = next(stream)
    if head & 0xe0:
        raise ValueError("invalid header: 0x%02x" % head)
    endian = 'big' if head & 0x10 else 'little'
    length = head & 0x0f  # number of bytes representing nbits
    nbits = 0
    for j in range(length):
        nbits |= next(stream) << 8 * j
    return endian, nbits

def sc_scan_block(stream, stats):
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

    # consume block data
    nconsume = max(1, n) * k   # size of block data to consume below
    if stats.get('count'):
        if n == 0:
            data = bytes(islice(stream, k))
            stats['count'][0] += bitarray(buffer=data).count()
            nconsume = 0
        else:
            stats['count'][n] += k

    next(islice(stream, nconsume, nconsume), None)

    return True

def sc_stat(stream, count=False):
    """sc_stat(stream, count=False) -> dict

Scan a byte stream produced by `sc_encode()` and return statistics
about its encoded blocks.  The `blocks` entry is a list of length 5
containing the number of blocks of each type.

When `count` is true, the `count` entry contains the number of
represented one-bits in blocks of each type.
"""
    stream = iter(stream)
    endian, nbits = sc_decode_header(stream)

    stats = {'endian': endian,
             'nbits': nbits,
             'blocks': 5 * [0]}
    if count:
        stats['count'] = 5 * [0]

    while sc_scan_block(stream, stats):
        pass

    return stats

# ---------------------------------------------------------------------------

import unittest
from random import choice

from bitarray.util import sc_encode, sc_decode, ones, random_k, random_p


class Tests(unittest.TestCase):

    def test_empty(self):
        blob = b"\x01\x00\0"
        self.assertEqual(sc_stat(blob),
                         {'endian': 'little',
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
            stat = sc_stat(blob, count=True)
            self.assertEqual(stat['blocks'], blocks)
            self.assertEqual(stat['count'], 5 * [0])
            self.assertEqual(sc_decode(blob), bitarray(8))

    def test_untouch(self):
        blob = b"\x01\x07\x01\x73\0XYZ"
        stream = iter(blob)
        stat = sc_stat(stream, count=True)
        self.assertEqual(stat,
                         {'endian': 'little',
                          'nbits': 7,
                          'blocks': [1, 0, 0, 0, 0],
                          'count': [5, 0, 0, 0, 0]})
        self.assertEqual(next(stream), ord('X'))
        stream = iter(blob)
        self.assertEqual(sc_decode(stream), bitarray("1100111"))
        self.assertEqual(next(stream), ord('X'))

    def test_ones(self):
        for n in range(500):
            a = ones(n)
            blob = sc_encode(a)
            stat = sc_stat(blob, count=True)
            self.assertEqual(stat['nbits'], n)
            self.assertEqual(stat['count'][0], n)

    def test_random_raw(self):
        for n in range(500):
            a = random_p(n, endian=choice(['little', 'big']))
            cnt = a.count()
            blob = sc_encode(a)
            stat = sc_stat(blob, count=True)
            self.assertEqual(stat['nbits'], n)
            self.assertEqual(stat['count'][0], cnt)

    def test_random_large(self):
        n = 20_000_000
        k = 1
        for _ in range(13):
            a = random_k(n, k)
            self.assertEqual(a.count(), k)
            stat = sc_stat(sc_encode(a), count=True)
            #print(len(a), k, stat['blocks'])
            self.assertEqual(sum(stat['count']), k)
            k *= 4

    def test_end_of_stream(self):
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
        stat = sc_stat(b, True)
        self.assertEqual(stat['blocks'], [2, 147, 3, 1, 1])
        self.assertEqual(stat['count'], [1 << 16, 374, 427, 220, 2])
        self.assertEqual(a, sc_decode(b))

        a.reverse()
        b = sc_encode(a)
        self.assertEqual(sc_stat(b)['blocks'], [2, 256, 254, 3, 0])
        self.assertEqual(a, sc_decode(b))


if __name__ == '__main__':
    unittest.main()
