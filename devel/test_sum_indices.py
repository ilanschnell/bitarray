"""
In both ssqi() (in _util.c) and sum_indices() (in util.py), we divide our
bitarray into equally sized blocks in order to calculate the sum of active
indices.  We use the same trick but for different reasons:

  (a) in ssqi(), we want to loop over bytes (blocks of 8 bits) and use
      lookup tables (for sum z_j [**2])

  (b) in sum_indices() we want to loop over blocks of smaller bitarrays
      in order to keep the summation in ssqi() from overflowing

The trick is to write

    x_j = y_j + z_j        where  y_j = y  : if bit j is active
                                        0  : otherwise

for each block.  Here, j is the index within each block.
That is, j is in range(block size).
Using the above, we get:

    sum x_j   =   k * y  +  sum z_j

where k is the bit count (per block).  And:

    sum x_j**2   =   k * y**2  +  2 * sum z_j * y  +  sum z_j**2

These are the sums for each block and their sum (over all blocks) is what
we are interested in.

                   (a)  ssqi()          (b)  sum_indices()
------------------------------------------------------------
block              c (char)             block (bitarray)
block size         8                    n
i                  byte index           block index
y                  8 * i                n * i
k                  count_table[c]       block.count()
z1 = sum z_j       sum_table[c]         _ssqi(block)
z2 = sum z_j**2    sum_sqr_table[c]     _ssqi(block, 2)
"""
import math
import unittest
from random import choice, getrandbits, randint, randrange, sample

from bitarray import bitarray, frozenbitarray
from bitarray.util import (zeros, ones, gen_primes, urandom,
                           _ssqi, sum_indices)


# Limits of bitarray size in _ssqi()
# ----------------------------------
# These limits are calculated and tested in SSQI_Tests below.
# They are used in the C implementation of the internal function _ssqi().
# The public Python function sum_indices() does NOT impose any limits
# on the size of bitarrays it can compute.
SSQI_LIMIT = (None, 6_074_001_000, 3_810_778)


N19 = 1 << 19  # 512 Kbit =  64 KB
N20 = 1 << 20  #   1 Mbit = 128 KB
N21 = 1 << 21  #   2 Mbit = 256 KB
N22 = 1 << 22  #   4 Mbit = 512 KB
N23 = 1 << 23  #   8 Mbit =   1 MB
N28 = 1 << 28  # 256 Mbit =  32 MB
N30 = 1 << 30  #   1 Gbit = 128 MB
N31 = 1 << 31  #   2 Gbit = 256 MB
N32 = 1 << 32  #   4 Gbit = 512 MB
N33 = 1 << 33  #   8 Gbit =   1 GB

MAX_UINT64 = (1 << 64) - 1
ENDIANS = ('little', 'big')


def sum_range(n):
    "Return sum(range(n))"
    return n * (n - 1) // 2

def sum_sqr_range(n):
    "Return sum(i * i for i in range(n))"
    return n * (n - 1) * (2 * n - 1) // 6


class SumRangeTests(unittest.TestCase):

    def test_sum_range(self):
        for n in range(1000):
            self.assertEqual(sum_range(n), sum(range(n)))

    def test_sum_sqr_range(self):
        for n in range(1000):
            self.assertEqual(sum_sqr_range(n), sum(i * i for i in range(n)))

    def test_mode(self):
        for n in range(1000):
            for mode, f in [(1, sum_range),
                            (2, sum_sqr_range)]:
                sum_ones = 3 if mode == 1 else 2 * n - 1
                sum_ones *= n * (n - 1)
                sum_ones //= 6
                self.assertEqual(sum_ones, f(n))

    def test_o2(self):
        for n in range(1000):
            o1 = n * (n - 1) // 2
            o2, r = divmod(o1 * (2 * n - 1), 3)
            self.assertEqual(r, 0)
            self.assertEqual(o2, sum_sqr_range(n))


class ExampleImplementationTests(unittest.TestCase):

    def sum_indices(self, a, mode=1):
        n = 503  # block size in bits
        nblocks = (len(a) + n - 1) // n  # number of blocks
        sm = 0
        for i in range(nblocks):
            y = n * i
            block = a[y : y + n]

            k = block.count()
            z1 = _ssqi(block)
            self.assertEqual(
                # Note that j are indices within each block.
                # Also note that we use len(block) instead of block_size,
                # as the last block may be truncated.
                z1, sum(j for j in range(len(block)) if block[j]))

            if mode == 1:
                x = k * y + z1
            else:
                z2 = _ssqi(block, 2)
                x = (k * y + 2 * z1) * y + z2

            # x is the sum [of squares] of indices for each block
            self.assertEqual(
                # Note that here t are indices of the full bitarray a.
                x, sum(t ** mode for t in range(y, y + len(block)) if a[t]))

            sm += x

        return sm

    def test_sum_indices(self):
        for _ in range(100):
            n = randrange(10_000)
            a = urandom(n)
            mode = randint(1, 2)
            self.assertEqual(self.sum_indices(a, mode), sum_indices(a, mode))


class SumIndicesUtil(unittest.TestCase):

    def check_explicit(self, S):
        for s, r1, r2 in [
                ("", 0, 0), ("0", 0, 0), ("1", 0, 0), ("11", 1, 1),
                ("011", 3, 5), ("001", 2, 4), ("0001100", 7, 25),
                ("00001111", 22, 126), ("01100111 1101", 49, 381),
        ]:
            for a in [bitarray(s, choice(ENDIANS)),
                      frozenbitarray(s, choice(ENDIANS))]:
                self.assertEqual(S(a, 1), r1)
                self.assertEqual(S(a, 2), r2)
                self.assertEqual(a, bitarray(s))

    def check_wrong_args(self, S):
        self.assertRaises(TypeError, S, '')
        self.assertRaises(TypeError, S, 1.0)
        self.assertRaises(TypeError, S)
        for mode in -1, 0, 3, 4:
            self.assertRaises(ValueError, S, bitarray("110"), mode)

    def check_sparse(self, S, n, k, mode=1, freeze=False, inv=False):
        a = zeros(n, choice(ENDIANS))
        indices = sample(range(n), k)
        a[indices] = 1
        res = sum(indices) if mode == 1 else sum(i * i for i in indices)

        if inv:
            a.invert()
            sum_ones = 3 if mode == 1 else 2 * n - 1
            sum_ones *= n * (n - 1)
            sum_ones //= 6
            res = sum_ones - res

        if freeze:
            a = frozenbitarray(a)

        c = a.copy()
        self.assertEqual(a.count(), n - k if inv else k)
        self.assertEqual(S(a, mode), res)
        self.assertEqual(a, c)


class SSQI_Tests(SumIndicesUtil):

    modes = [(1, sum_range),
             (2, sum_sqr_range)]

    def test_calculate_limits(self):
        # calculation of limits used in ssqi() (in _util.c)
        for mode, f in self.modes:
            lo = 0
            hi = MAX_UINT64
            while hi > lo + 1:
                n = (lo + hi) // 2
                if f(n) > MAX_UINT64:
                    hi = n
                else:
                    lo = n
            self.assertTrue(f(n) < MAX_UINT64)
            self.assertTrue(f(n + 1) > MAX_UINT64)
            self.assertEqual(n, SSQI_LIMIT[mode])

    def test_overflow(self):
        # _ssqi() is limited to bitarrays of about 6 Gbit (4 Mbit mode=2).
        # This limit is never reached because sum_indices() uses
        # a much smaller block size for practical reasons.
        for mode, f in self.modes:
            n = SSQI_LIMIT[mode]
            a = ones(n)
            self.assertTrue(f(len(a)) <= MAX_UINT64)
            self.assertEqual(_ssqi(a, mode), f(n))
            a.append(1)
            self.assertTrue(f(len(a)) > MAX_UINT64)
            self.assertRaises(OverflowError, _ssqi, a, mode)

    def test_explicit(self):
        self.check_explicit(_ssqi)

    def test_wrong_args(self):
        self.check_wrong_args(_ssqi)

    def test_primes(self):
        n = 3_800_000
        endian = choice(['little', 'big'])
        a = gen_primes(n, endian)
        self.assertEqual(_ssqi(a, 1),           493_187_952_850)
        self.assertEqual(_ssqi(a, 2), 1_234_421_634_142_352_974)

    def test_sparse(self):
        for _  in range(100):
            n = randint(2, 3_810_778)
            k = randrange(min(1_000, n // 2))
            mode = randint(1, 2)
            freeze = getrandbits(1)
            inv = getrandbits(1)
            self.check_sparse(_ssqi, n, k, mode, freeze, inv)


class SumIndicesTests(SumIndicesUtil):

    def test_explicit(self):
        self.check_explicit(sum_indices)

    def test_wrong_args(self):
        self.check_wrong_args(sum_indices)

    def test_random_sample(self):
        n = N31
        for k in 1, 31, 503:
            mode = randint(1, 2)
            freeze = getrandbits(1)
            inv = getrandbits(1)
            self.check_sparse(sum_indices, n, k, mode, freeze, inv)

    def test_primes(self):
        n = 10_000_000
        endian = choice(['little', 'big'])
        a = gen_primes(n, endian)
        self.assertEqual(sum_indices(a, 1),          3_203_324_994_356)
        self.assertEqual(sum_indices(a, 2), 21_113_978_675_102_768_574)

    def test_ones(self):
        for m in range(19, 32):
            n = randrange(1 << m)
            mode = randint(1, 2)
            freeze = getrandbits(1)
            self.check_sparse(sum_indices, n, 0, mode, freeze, inv=True)

    def test_sum_random(self):
        for _  in range(50):
            n = randrange(1 << randrange(19, 32))
            k = randrange(min(1_000, n // 2))
            mode = randint(1, 2)
            freeze = getrandbits(1)
            inv = getrandbits(1)
            self.check_sparse(sum_indices, n, k, mode, freeze, inv)

    def test_hypot(self):
        a = urandom(10_000)
        self.assertAlmostEqual(math.sqrt(sum_indices(a, 2)),
                               math.hypot(*list(a.search(1))))


class VarianceTests(unittest.TestCase):

    def variance(self, a, mu=None):
        si = sum_indices(a)
        k = a.count()
        if mu is None:
            mu = si / k
        return (sum_indices(a, 2) - 2 * mu * si) / k + mu * mu

    def variance_values(self, values, mu=None):
        k = len(values)
        if mu is None:
            mu = sum(values) / k
        return sum((x - mu) ** 2 for x in values) / k

    def test_variance(self):
        for _ in range(1_000):
            n = randrange(1, 1_000)
            k = randint(1, max(1, n // 2))
            indices = sample(range(n), k)
            a = zeros(n)
            a[indices] = 1
            mean = sum(indices) / len(indices)
            self.assertAlmostEqual(self.variance(a),
                                   self.variance_values(indices))
            self.assertAlmostEqual(self.variance(a, mean),
                                   self.variance_values(indices, mean))
            mean = 20.5
            self.assertAlmostEqual(self.variance(a, mean),
                                   self.variance_values(indices, mean))


def test_ones():

    for n in [SSQI_LIMIT[2],
              SSQI_LIMIT[2] + 1,
              SSQI_LIMIT[1],
              SSQI_LIMIT[1] + 1,
              N33,
              2 * N33]:
        a = ones(n)
        print("n =    %32d  %6.2f Gbit    %6.2f GB" % (n, n / N30, n / N33))
        print("2^64 = %32d" % (1 << 64))
        res = sum_indices(a)
        print("sum =  %32d" % res)
        assert res == sum_range(n)

        res = sum_indices(a, 2)
        print("sum2 = %32d" % res)
        assert res == sum_sqr_range(n)

        print()

    print("OK")


if __name__ == "__main__":
    import sys
    if '--ones' in sys.argv:
        test_ones()
        sys.exit()
    unittest.main()
