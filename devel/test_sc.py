import bz2
import sys
import gzip
import random
import struct
import unittest
from time import perf_counter

from bitarray import bitarray
from bitarray.util import (
    ones, random_p,
    serialize, deserialize,
    sc_encode, sc_decode, sc_stat,
    vl_encode, vl_decode,
)
from bitarray.test_util import SC_Util


class SC_Tests(unittest.TestCase, SC_Util):

    def test_block_type3(self):
        a = bitarray(1 << 24, 'little')
        # Start with the largest type 3 index so every population requires
        # type 3.
        indices = self.sample_with_highest(len(a), k=255)

        for k, index in enumerate(indices, 1):
            a[index] = 1
            b = bytearray(b'\x04\x00\x00\x00\x01\xc3')
            b.append(k)
            for i in sorted(indices[:k]):
                b.extend(struct.pack("<I", i)[:3])
            b.append(0)  # stop byte
            self.assertEqual(sc_stat(b)['blocks'], [0, 0, 0, 1, 0])
            self.assertEqual(sc_decode(b), a)
            self.assertEqual(sc_encode(a), b)

    def test_block_type4(self):
        a = bitarray(1 << 28, 'little')
        # 16 type 3 blocks require 32 header bytes.  A type 4 block
        # requires only a 2-byte header, but adds one byte per index.
        # So for population k, we have a tie when:
        #
        #    32 + 3k = 2 + 4k   ->   k = 30
        #
        # At the tie (population 30), the encoder prefers type 3.
        # Start with the largest type 4 index so every population requires
        # type 4.
        indices = self.sample_with_highest(len(a), k=29)

        for k, index in enumerate(indices, 1):
            a[index] = 1
            b = bytearray(b'\x04\x00\x00\x00\x10\xc4')
            b.append(k)
            for i in sorted(indices[:k]):
                b.extend(struct.pack("<I", i))
            b.append(0)  # stop byte
            self.assertEqual(sc_stat(b)['blocks'], [0, 0, 0, 0, 1])
            self.assertEqual(sc_decode(b), a)
            self.assertEqual(sc_encode(a), b)

    def test_example(self):
        n = 1 << 28
        a = bitarray(n, 'little')
        a[:1 << 16] = 1
        for i in range(2, 1 << 17):
            a[n // i] = 1
        b = sc_encode(a)
        stat = sc_stat(b)
        self.assertEqual(stat, {'endian': 'little',
                                'nbits': n,
                                'blocks': [2, 653, 12, 1, 1]})
        self.assertEqual(a, sc_decode(b))

        a.reverse()
        b = sc_encode(a)
        self.assertEqual(sc_stat(b)['blocks'], [2, 768, 252, 15, 0])
        self.assertEqual(a, sc_decode(b))

    def test_random(self):
        count_exponent = (12, 19, 11, 3, 0)

        state = random.getstate()
        self.addCleanup(random.setstate, state)
        random.seed(4567)
        n = 1 << 27

        a = random_p(n)
        i = 1
        # Each bit has probability (1 / 2) ** i.
        while a.any():
            blob = sc_encode(a)
            b = sc_decode(blob)
            self.assertEqual(a, b)

            stat = sc_stat(blob)
            self.assertEqual(stat['nbits'], len(a))
            blocks = 5 * [0]
            block_type = sum(i > j for j in (4, 8, 16, 24))
            blocks[block_type] = 1 << count_exponent[block_type]
            self.assertEqual(stat['blocks'], blocks)

            a &= random_p(n, 1 / 16)
            i += 4


# ---------------------------------------------------------------------------

def p_range():
    n = 1 << 28
    p = 1.0
    a = ones(n)
    print("        p          ratio         raw"
          "    type 1    type 2    type 3    type 4")
    print("   " + 73 *'-')
    while p > 1e-8:
        b = sc_encode(a)
        blocks = sc_stat(b)['blocks']
        print('  %11.8f  %11.8f  %8d  %8d  %8d  %8d  %8d' %
              tuple([p, len(b) / (n / 8)] + blocks))
        assert a == sc_decode(b)
        a &= random_p(n)
        p /= 2

def compare():
    n = 1 << 26
    a = random_p(n, 1.0 / 1024)

    raw = a.tobytes()
    print(20 * ' ' +  "compress (ms)   decompress (ms)             ratio")
    print(70 * '-')
    for name, f_e, f_d in [
            ('serialize', serialize, deserialize),
            ('vl', vl_encode, vl_decode),
            ('sc' , sc_encode, sc_decode),
            ('gzip', gzip.compress, gzip.decompress),
            ('bz2', bz2.compress, bz2.decompress)]:
        x = a if name in ('serialize', 'vl', 'sc') else raw
        t0 = perf_counter()
        b = f_e(x)  # compression
        t1 = perf_counter()
        c = f_d(b)  # decompression
        t2 = perf_counter()
        print("    %-11s  %16.3f  %16.3f  %16.4f" %
              (name, 1000 * (t1 - t0), 1000 * (t2 - t1), len(b) / len(raw)))
        assert c == x


if __name__ == '__main__':
    if '--disp' in sys.argv:
        random.seed(123)
        compare()
        p_range()
        sys.exit(0)
    unittest.main()
