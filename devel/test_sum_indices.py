import random
import unittest

from bitarray.util import zeros, ones, sum_indices, _sum_sqr_indices


class SumIndicesTests(unittest.TestCase):

    def test_overflow_mode2(self):
        n = 8 << 27
        self.assertEqual(n, 1 << 30)
        a = ones(n)
        self.assertEqual(sum_indices(a, 2), n * (n-1) * (2*n-1) // 6)
        a.append(1)
        self.assertRaises(OverflowError, sum_indices, a, 2)

    def test_random(self):
        n = 8 << 28
        self.assertEqual(n, 1 << 31)
        indices = random.sample(range(n), 1 << 22)
        a = zeros(n)
        a[indices] = 1
        self.assertEqual(sum_indices(a), sum(indices))
        self.assertEqual(_sum_sqr_indices(a), sum(i * i for i in indices))


def test_ones():
    N30 = 1 << 30  # 1 Gbit = 128 Mbyte
    N32 = 1 << 32  # 4 Gbit = 512 Mbyte
    N33 = 1 << 33  # 8 Gbit =   1 GByte

    for n in [N30, 2 * N30, N32, 8 * N30, 16 * N30]:
        a = ones(n)
        print("n =    %32d  %6.2f Gbit    %6.2f GByte" % (n, n / N30, n / N33))
        print("2^63 = %32d" % (1 << 63))
        res = sum_indices(a)
        print("sum =  %32d" % res)
        assert res == n * (n - 1) // 2;

        res = _sum_sqr_indices(a)
        print("sum2 = %32d" % res)
        assert res == n * (n-1) * (2*n-1) // 6

        print()

    print("OK")


if __name__ == "__main__":
    test_ones()
    unittest.main()

