import random
import unittest

from bitarray import bitarray
from bitarray.util import zeros, ones, sum_indices, _sum_sqr_indices


N19 = 1 << 19  # 512 Kbit =  64 Kbyte
N20 = 1 << 20  #   1 Mbit = 128 Kbyte
N21 = 1 << 21  #   2 Mbit = 256 Kbyte
N22 = 1 << 22  #   4 Mbit = 512 Kbyte
N30 = 1 << 23  #   8 Mbit =   1 Mbyte
N30 = 1 << 30  #   1 Gbit = 128 Mbyte
N31 = 1 << 31  #   2 Gbit = 256 Mbyte
N32 = 1 << 32  #   4 Gbit = 512 Mbyte
N33 = 1 << 33  #   8 Gbit =   1 GByte

class SumIndicesTests(unittest.TestCase):

    def test_overflow_mode2(self):
        n = N30
        self.assertEqual(n, 8 << 27)
        a = ones(n)
        self.assertEqual(sum_indices(a, 2), n * (n-1) * (2*n-1) // 6)
        a.append(1)
        # in C code we check for nbytes > (1 << 27)
        self.assertRaises(OverflowError, sum_indices, a, 2)

    def test_random_sample(self):
        n = N31
        for k in 0, 1, 31, 503, N22:
            indices = random.sample(range(n), k)
            a = zeros(n)
            a[indices] = 1
            self.assertEqual(sum_indices(a), sum(indices))
            self.assertEqual(_sum_sqr_indices(a), sum(i * i for i in indices))

    def test_random_size(self):
        for _ in range(1000):
            n = random.randrange(N21)
            a = ones(n)
            self.assertEqual(sum_indices(a), n * (n - 1) // 2)
            self.assertEqual(_sum_sqr_indices(a), n * (n-1) * (2*n-1) // 6)

            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), n)


def test_ones():

    for n in [N30, N31, N32, N33, 2 * N33]:
        a = ones(n)
        print("n =    %32d  %6.2f Gbit    %6.2f GByte" % (n, n / N30, n / N33))
        print("2^63 = %32d" % (1 << 63))
        res = sum_indices(a)
        print("sum =  %32d" % res)
        assert res == n * (n - 1) // 2

        res = _sum_sqr_indices(a)
        print("sum2 = %32d" % res)
        assert res == n * (n-1) * (2*n-1) // 6

        print()

    print("OK")


if __name__ == "__main__":
    import sys
    if '--ones' in sys.argv:
        test_ones()
        sys.exit()
    unittest.main()
