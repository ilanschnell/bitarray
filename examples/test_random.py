"""
Statistical Tests for random functions in bitarray.util
-------------------------------------------------------

These are statistical tests.  They do not test any basic functionality of
random functions.  Those are already tested in the regular utility tests.
Therefore, and because these tests take longer to run, we decided to put
them in a separate file.
"""
import sys
import unittest
from math import sqrt
from collections import Counter
from random import randrange, random

from bitarray import bitarray
from bitarray.util import zeros, urandom, random_p
from bitarray.util import _RandomP  # type: ignore


SMALL_P = _RandomP().SMALL_P


class Util(unittest.TestCase):

    def check_normal_dist(self, n, p, x):
        if n < 10 or p * (1.0 - p) < 0.01:
            return
        mu = n * p
        sigma = sqrt(n * p * (1.0 - p))
        msg = "n=%d  p=%f  mu=%f  sigma=%f  x=%f" % (
            n, p, mu, sigma, x)
        self.assertTrue(abs(x - mu) < 10.0 * sigma, msg)

    def count_ith(self, arrays):
        """
        Given a list of bitarrays, count sums of the i-th elements
        in each bitarray and return them in a Counter object.
        """
        m = len(arrays)     # number of bitarrays
        self.assertTrue(m > 0)
        n = len(arrays[0])  # lenght of each bitarray

        b = bitarray()
        for a in arrays:
            self.assertEqual(len(a), n)
            b.extend(a)
        self.assertEqual(len(b), m * n)

        c = Counter()
        for i in range(n):
            #sm = sum(arrays[j][i] for j in range(m))
            sm = b.count(1, i, m * n, n)
            c[sm] += 1
        self.assertEqual(c.total(), n)
        return c


class UtilTests(Util):

    def test_count_ith(self):
        arrays = [bitarray("0011101"),
                  bitarray("1010100"),
                  bitarray("1011001")]
        #             sums: 2032202
        c = self.count_ith(arrays)
        self.assertEqual(c.total(), 7)  # lenght of each bitarray
        self.assertEqual(c[0], 2)
        self.assertEqual(c[1], 0)
        self.assertEqual(c[2], 4)
        self.assertEqual(c[3], 1)


class URandomTests(Util):

    def test_count(self):
        a = urandom(10_000_000)
        self.assertTrue(4_980_000 <= a.count() <= 5_020_000)

    def test_stat(self):
        c = Counter(urandom(100).count() for _ in range(10_000))
        self.assertTrue(set(c) <= set(range(1001)))
        if sys.version_info[:2] >= (3, 10):
            self.assertEqual(c.total(), 10_000)
        x = sum(c[k] for k in range(46, 51))
        # p = 0.355694   mean = 3556.938100   stdev = 47.872301
        self.assertTrue((x - 3557) <= 479)


class Random_P_Tests(Util):

    def test_all_bits_active(self):
        for _ in range(1000):
            n = randrange(1000)
            p = 1.0 - 1.1 * SMALL_P * random()
            cum = zeros(n)
            for i in range(15):
                a = random_p(n, p)
                self.check_normal_dist(n, p, a.count())
                cum |= a
            self.assertEqual(len(cum), n)
            self.assertTrue(cum.all())

    def test_bits_evenly(self):
        n = 4000
        c = d = N = 0
        for _ in range(25_000):
            p = 1.5 * SMALL_P * random()
            a = random_p(n, p)
            tot = a.count()
            self.check_normal_dist(n, p, tot)
            c1 = a.count(1, 0, n // 2)  # bits in lower half
            c2 = a.count(1, n // 2, n)  #         upper
            self.assertEqual(c1 + c2, tot)
            if c1 == c2:
                continue

            d1 = a.count(1, 0, n, 2)  # bits in even positions
            d2 = a.count(1, 1, n, 2)  #         odd
            self.assertEqual(d1 + d2, tot)
            if d1 == d2:
                continue

            N += 1
            if c1 > c2:
                c += 1
            if d1 > d2:
                d += 1
        self.check_normal_dist(N, 0.5, c)
        self.check_normal_dist(N, 0.5, d)

    def test_uniform(self):
        arrays = [random_p(100_000, 0.3) for _ in range(100)]
        for a in arrays:
            # for each bitarray see if population is within expectation
            self.assertTrue(abs(a.count() - 30_000) < 1_449)

        c = self.count_ith(arrays)
        self.assertTrue(abs(c[30] - 8_678) <= 890)
        x = sum(c[k] for k in range(20, 31))
        # p = 0.540236   mean = 54023.639245   stdev = 157.601089
        self.assertTrue(abs(x - 54_024) <= 1_576)
        self.assertEqual(c.total(), 100_000)

    def test_tiny_p(self):
        for n in 4, 10, 1000:
            for p in 1e-9, 1e-12, 1e-15, 1e-18:
                a = random_p(n, p)
                self.assertTrue(a.count() <= 1)

    def test_n4(self):
        # test "literal definition" case
        c = Counter(random_p(4, 0.25).count() for _ in range(100_000))
        self.assertTrue(abs(c[0] - 31_641) <=  1_471)
        self.assertTrue(abs(c[1] - 42_188) <=  1_562)
        self.assertTrue(abs(c[2] - 21_094) <=  1_290)
        self.assertEqual(c.total(), 100_000)

    def test_n10_p005(self):
        # test small p case
        c = Counter(random_p(10, 0.005).count() for _ in range(100_000))
        self.assertTrue(abs(c[0] - 95_111) <=    682)
        self.assertTrue(abs(c[1] -  4_779) <=    675)
        self.assertEqual(c.total(), 100_000)

    def test_n100_p375(self):
        # test random_combine()
        c = Counter(random_p(100, 0.375).count() for _ in range(100_000))
        x = sum(c[k] for k in range(37, 49))
        # p = 0.566139   mean = 56613.946454   stdev = 156.724462
        self.assertTrue(abs(x - 56_614) <= 1_567)
        self.assertEqual(c.total(), 100_000)

    def test_n100_p7(self):
        # general case
        c = Counter(random_p(100, p=0.7).count() for _ in range(100_000))
        x = sum(c[k] for k in range(61, 71))
        # p = 0.516672   mean = 51667.168798   stdev = 158.025965
        self.assertTrue(abs(x - 51_667) <= 1_580)
        self.assertEqual(c.total(), 100_000)


if __name__ == '__main__':
    unittest.main(verbosity=2)
