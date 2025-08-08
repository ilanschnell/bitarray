"""
In both _util.c (ssqi() mode=2) and util.py (sum_indices()), we use the
same trick but for different reasons:

  (a) in _util.c, we want to loop over bytes for speed and create
      lookup tables (for sum z_j (**2))

  (b) in util.py we loop over smaller bitarrays in order to keep the sum
      in _util.c from overflowing

The trick is to write

    x_j = y_j + z_j        where  y_j = y  : if bit j is active
                                        0  : otherwise

for each byte / block, and j in range(block_size).
Using the above, we get:

    sum x_j   =   y * bit_count  +  sum z_j

and

    sum x_j**2   =   y**2 * bit_count  +  2 * y * sum z_j  +  sum z_j**2


              (a)                  (b)
---------------------------------------------------------
block         c (char)             block (bitarray)
block size    8                    len(block)
i             byte index           block index
y             8 * i                len(block) * i
bit_count     count_table[c]       block.count()
sum z_j       sum_table[c]         sum_indices(block)
sum z_j**2    sum_sqr_table[c]     sum_indices(block, 2)
"""
import unittest
from random import getrandbits, randint, randrange, sample

from bitarray.util import zeros, ones, urandom, _ssqi, sum_indices
from bitarray.test_util import SumIndicesUtil


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
        nbits = len(a)
        block_size = 503  # block size in bits
        nblocks = (nbits + block_size - 1) // block_size  # number of blocks
        sm = 0
        for i in range(nblocks):
            y = block_size * i
            block = a[y : y + block_size]
            if mode == 1:
                z = sum_indices(block)
                self.assertEqual(
                    # Note that j are indices within each block.
                    # Also note that we use len(block) instead of block_size,
                    # as the last block may be truncated.
                    z, sum(j for j in range(len(block)) if block[j]))

                x = y * block.count() + z
                self.assertEqual(
                    # Note that k are indices of the full bitarray a.
                    x, sum(k for k in range(y, y + len(block)) if a[k]))
            else:
                z = sum_indices(block, 2)
                x = y * y * block.count() + 2 * y * sum_indices(block) + z
            sm += x
        return sm

    def test_sum_indices(self):
        for _ in range(100):
            n = randrange(100_000)
            a = urandom(n)
            mode = randint(1, 2)
            self.assertEqual(self.sum_indices(a, mode), sum_indices(a, mode))


class SSQI_Tests(unittest.TestCase):

    # Note carefully that the limits that are calculated and tested here
    # are limits used in internal function _ssqi().
    # The public Python function sum_indices() does NOT impose any limits
    # on the size of bitarrays it can compute.

    def test_limits(self):
        # calculation of limits used in ssqi() (in _util.c)
        for f, res in [(sum_range, 6_074_001_000),
                       (sum_sqr_range, 3_810_778)]:
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
            self.assertEqual(n, res)

    def test_overflow_mode1(self):
        # _ssqi() is limited to bitarrays of about 6 Gbit (4 Mbit mode=2).
        # This limit is never reached because sum_indices() uses
        # a much smaller block size for practical reasons.
        for mode, f, n in [(1, sum_range, 6_074_001_000),
                           (2, sum_sqr_range, 3_810_778)]:
            a = ones(n)
            self.assertTrue(f(len(a)) <= MAX_UINT64)
            self.assertEqual(_ssqi(a, mode), f(n))
            a.append(1)
            self.assertTrue(f(len(a)) > MAX_UINT64)
            self.assertRaises(OverflowError, _ssqi, a, mode)


class SumIndicesTests(SumIndicesUtil):

    def test_urandom(self):
        self.check_urandom(sum_indices, 1_000_003)

    def test_random_sample(self):
        n = N31
        for k in 1, 31, 503:
            mode = randint(1, 2)
            freeze = getrandbits(1)
            inv = getrandbits(1)
            self.check_sparse(sum_indices, n, k, mode, freeze, inv)

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

    for n in [3_810_778,
              3_810_779,
              6_074_001_000,
              6_074_001_001,
              N33, 2 * N33]:
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
