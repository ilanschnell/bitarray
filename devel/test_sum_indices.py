import unittest
from random import choice, getrandbits, randrange, sample

from bitarray import frozenbitarray
from bitarray.util import zeros, ones, sum_indices, _sum_sqr_indices


N19 = 1 << 19  # 512 Kbit =  64 Kbyte
N20 = 1 << 20  #   1 Mbit = 128 Kbyte
N21 = 1 << 21  #   2 Mbit = 256 Kbyte
N22 = 1 << 22  #   4 Mbit = 512 Kbyte
N23 = 1 << 23  #   8 Mbit =   1 Mbyte
N28 = 1 << 28  # 256 Mbit =  32 Mbyte
N30 = 1 << 30  #   1 Gbit = 128 Mbyte
N31 = 1 << 31  #   2 Gbit = 256 Mbyte
N32 = 1 << 32  #   4 Gbit = 512 Mbyte
N33 = 1 << 33  #   8 Gbit =   1 GByte

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


class SumIndicesTests(unittest.TestCase):

    def test_random_sample(self):
        n = N31
        for k in 0, 1, 31, 503:
            indices = sample(range(n), k)
            a = zeros(n)
            a[indices] = 1
            if getrandbits(1):
                a = frozenbitarray(a)
            self.assertEqual(sum_indices(a), sum(indices))

    def test_sum_random(self):
        for _ in range(1000):
            n = randrange(N21)
            a = ones(n, choice(["little", "big"]))
            k = randrange(min(1_000, n // 2))
            indices = sample(range(n), k)
            a[indices] = 0
            if getrandbits(1):
                a = frozenbitarray(a)
            c = a.copy()
            self.assertEqual(sum_indices(a), sum_range(n) - sum(indices))
            # ensure a wasn't changed
            self.assertEqual(a, c)


class SumSqrIndicesTests(unittest.TestCase):

    # In both _util.c (sum_indices() mode=2) and util.py (_sum_sqr_indices()),
    # we use the same trick but for different reasons: (a) in _util.c, we want
    # to loop over bytes for speed (b) in util.py we loop over smaller
    # bitarrays in order to keep the sum in _util.c from overflowing.
    # The trick is to write for each byte / block:
    #
    #     sum x_j**2            j is the index within the block
    #
    # as (using x_j = y + z_j, where y is constant within the block):
    #
    #     y**2 * sum 1  +  2 * y * sum z_j  +  sum z_j**2
    #
    # y is the block size times i (the block index)
    #
    #               (a)                  (b)
    # ---------------------------------------------------------
    # block         c (char)             block (bitarray)
    # i             byte index           block index
    # y             8 * i                len(block) * i
    # sum 1         count_table[c]       block.count()
    # sum z_j       sum_table[c]         sum_indices(block)
    # sum z_j**2    sum_sqr_table[c]     sum_indices(block, 2)

    def verify_overflow(self, a, overflow):
        i = a.nbytes - 1  # largest i
        # In the inner loop, what we add to sm has to be smaller
        # than 1 << 63, as sm might already be 1 << 63.
        sm = 1 << 63
        sm += 64 * i * i * 8  #   8 = max(count_table)
        sm += 16 * i * 28     #  28 = max(sum_table)
        sm += 140             # 140 = max(sum_sqr_table)
        self.assertEqual(overflow, sm > MAX_UINT64)
        # in C code we check for nbytes > (1 << 27)
        self.assertEqual(overflow, a.nbytes > 1 << 27)

    def test_overflow_mode2(self):
        n = N30
        self.assertEqual(n, 8 << 27)
        a = ones(n)
        self.verify_overflow(a, False)
        self.assertEqual(sum_indices(a, 2), sum_sqr_range(n))
        a.append(1)
        self.verify_overflow(a, True)
        self.assertRaises(OverflowError, sum_indices, a, 2)

    def test_random_sample(self):
        n = N31
        for k in 0, 1, 31, 503:
            indices = sample(range(n), k)
            a = zeros(n)
            a[indices] = 1
            if getrandbits(1):
                a = frozenbitarray(a)
            self.assertEqual(_sum_sqr_indices(a),
                             sum(i * i for i in indices))

    def test_sum_sqr_random(self):
        for _ in range(1000):
            n = randrange(N21)
            a = ones(n, choice(["little", "big"]))
            k = randrange(min(1_000, n // 2))
            indices = sample(range(n), k)
            a[indices] = 0
            if getrandbits(1):
                a = frozenbitarray(a)
            c = a.copy()
            res = sum_sqr_range(n) - sum(i * i for i in indices)
            self.assertEqual(_sum_sqr_indices(a), res)
            # ensure a wasn't changed
            self.assertEqual(a, c)


def test_ones():

    for n in [N30, N31, N32, N33, 2 * N33]:
        a = ones(n)
        print("n =    %32d  %6.2f Gbit    %6.2f GByte" % (n, n / N30, n / N33))
        print("2^63 = %32d" % (1 << 63))
        res = sum_indices(a)
        print("sum =  %32d" % res)
        assert res == sum_range(n)

        res = _sum_sqr_indices(a)
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
