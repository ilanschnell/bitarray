import bz2
import sys
import gzip
import random
import unittest
from time import perf_counter

from bitarray import bitarray
from bitarray.util import (
    ones, zeros, random_p,
    serialize, deserialize,
    sc_encode, sc_decode, sc_stat,
    vl_encode, vl_decode,
)
from bitarray.test_bitarray import show_info
from bitarray.test_util import SC_Util


class SC_Tests(unittest.TestCase, SC_Util):

    def test_get_max_pop(self):
        for n, m, k in [
                # type 1 headers have size 1
                (2,   3,   0),  # type 2 is never preferred
                (2,   4,   1),  # type 2 is preferred only at 1
                (2,  16,  13),
                (2,  32,  29),
                (2, 256, 253),  # type 2 is preferred through 253
                # Type 2 never reaches its population limit, because
                # the type 1 header is smaller.

                # type 2 and 3 headers have size 2
                (3,   1,   0),
                (3,   2,   1),
                (3,  16,  29),
                (3, 128, 253),
                (3, 129, 255),  # population-byte limit reached
                (3, 256, 255),

                (4,   1,   0),
                (4,   2,   1),
                (4,   4,   5),
                (4,   8,  13),
                (4,  16,  29),
                (4,  32,  61),
                (4, 128, 253),
                (4, 129, 255),  # population-byte limit reached
                (4, 256, 255),
        ]:
            self.assertEqual(self.get_max_pop(n, m), k)

        for m in range(1, 257):
            # Type 3 and 4 maximal population behave the same way.
            self.assertEqual(self.get_max_pop(3, m),
                             self.get_max_pop(4, m))

    def test_header(self):
        # test self.header() utility
        for n in [
                0, 1,
                255, 256,
                65535, 65536,
                (1 << 24) - 1, 1 << 24,
        ]:
            blob = self.header(n) + b'\0'
            a = sc_decode(blob)
            self.assertEqual(len(a), n)
            self.assertEqual(a.endian, 'little')
            self.assertEqual(blob, sc_encode(a))

    def test_alternate(self):
        a = 8 * (zeros(256, 'little') + ones(256, 'little'))
        blob = sc_encode(a)
        self.assertEqual(
            blob,
            b'\x02\x00\x10' + 8 * (b'\xa0' + b'\x20' + 32*b'\xff') + b'\0')
        self.assertEqual(sc_decode(blob), a)
        stat = sc_stat(blob)
        self.assertEqual(stat['blocks'], [8, 8, 0, 0, 0])

    def test_output_resize(self):
        # tests resizing output buffer
        n = random.randrange(500_000, 1000_000)
        a = random_p(n, 0.25)
        b = sc_encode(a)
        self.assertEqual(sc_decode(b), a)
        self.assertEqual(len(a), n)
        self.assertGreater(len(b), 32768)

    def test_block_type2(self):
        a = bitarray(65_536, 'little')
        # Start with the largest type 2 index so all 256 type 1 blocks are
        # needed.
        indices = self.sample_with_highest(len(a), 255)
        k_max = self.get_max_pop(2)
        self.assertEqual(k_max, 253)

        for k, index in enumerate(indices, 1):
            a[index] = 1
            b = self.make_blob(len(a), 2, indices[:k])
            # The index count byte of a type 2 block produced by sc_encode()
            # is never 254 or 255, because the type 1 header is smaller.
            self.check_stat(a, b, 2, check_encode=(k <= k_max))

    def test_block_type3(self):
        a = bitarray(1 << 24, 'little')
        # Start with the largest type 3 index so every population requires
        # type 3.
        indices = self.sample_with_highest(len(a), 255)
        k_max = self.get_max_pop(3)
        self.assertEqual(k_max, 255)

        for k, index in enumerate(indices, 1):
            a[index] = 1
            b = self.make_blob(len(a), 3, indices[:k])
            self.check_stat(a, b, 3)

    def test_block_type4(self):
        a = bitarray(1 << 28, 'little')
        # Start with the largest type 4 index so all 16 type 3 blocks are
        # needed.
        indices = self.sample_with_highest(len(a), 255)
        k_max = self.get_max_pop(4, 16)
        self.assertEqual(k_max, 29)

        for k, index in enumerate(indices, 1):
            a[index] = 1
            b = self.make_blob(len(a), 4, indices[:k])
            self.check_stat(a, b, 4, check_encode=(k <= k_max))

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
                                'blocks': [2, 653, 12, 2, 1]})
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
        nbits = 1 << 27

        a = random_p(nbits)
        i = 1
        # Each bit has probability (1 / 2) ** i.
        while a.any():
            blob = sc_encode(a)
            b = sc_decode(blob)
            self.assertEqual(a, b)

            stat = sc_stat(blob)
            self.assertEqual(stat['nbits'], len(a))
            blocks = 5 * [0]
            n = sum(i > j for j in (4, 8, 16, 24))
            blocks[n] = 1 << count_exponent[n]
            self.assertEqual(stat['blocks'], blocks)

            a &= random_p(nbits, 1 / 16)
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
    show_info()
    unittest.main()
