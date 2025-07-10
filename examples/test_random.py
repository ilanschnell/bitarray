"""
Statistical Tests for Random Functions in bitarray.util
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
from bitarray.util import zeros, ones, urandom, random_p
from bitarray.util import _RandomP  # type: ignore


SMALL_P = _RandomP().SMALL_P


def count_each_index(arrays):
    """
    Given an iterable of bitarrays, count the sums of all bits at each
    index and return those counts in a Counter object.
    For example, for a returned Counter c, c[2] = 4 means that a sum of 2
    across all bitarrays occurs at 4 indices.
    """
    b = bitarray()
    n = None         # length of each bitarray
    for a in arrays:
        if n is None:
            n = len(a)
        elif len(a) != n:
            raise ValueError("bitarrays of same lenght expected")
        b.extend(a)
    if n is None:
        return Counter()
    return Counter(b.count(1, i, len(b), n) for i in range(n))


class CountEachIndexTests(unittest.TestCase):

    def test_example(self):
        arrays = [bitarray("0011101"),
                  bitarray("1010100"),
                  bitarray("1011001")]
        #             sums: 2032202
        c = count_each_index(arrays)
        self.assertEqual(c.total(), 7)  # lenght of each bitarray
        self.assertEqual(c[0], 2)
        self.assertEqual(c[1], 0)
        self.assertEqual(c[2], 4)
        self.assertEqual(c[3], 1)

    def test_random(self):
        for _ in range(1_000):
            m = randrange(10)
            n = randrange(10) if m else 0
            arrays = [urandom(n) for _ in range(m)]
            c = count_each_index(arrays)
            self.assertEqual(c.total(), n)
            for j in range(m + 1):
                self.assertTrue(0 <= c[j] <= n)

            c2 = Counter(sum(arrays[j][i] for j in range(m))
                         for i in range(n))
            self.assertEqual(c, c2)

            # generator
            gen = (arrays[j] for j in range(m))
            self.assertEqual(count_each_index(gen), c)
            self.assertEqual(list(gen), [])

    def test_empty(self):
        arrays = []
        for m in range(10):
            self.assertEqual(count_each_index(arrays), Counter())
            arrays.append(bitarray())

    def test_zeros_ones(self):
        for _ in range(1_000):
            m = randrange(10)
            n = randrange(10) if m else 0
            c = count_each_index(zeros(n) for _ in range(m))
            self.assertEqual(c[0], n)

            c = count_each_index(ones(n) for _ in range(m))
            self.assertEqual(c[m], n)

    def test_errors(self):
        C = count_each_index
        self.assertRaises(ValueError, C, "ABC")
        self.assertRaises(TypeError, C, [0, 1])
        self.assertRaises(ValueError, C, [bitarray("01"), bitarray("1")])


class Util(unittest.TestCase):

    def check_normal_dist(self, n, p, x):
        mu = n * p
        sigma = sqrt(n * p * (1.0 - p))
        msg = "n=%d  p=%f  mu=%f  sigma=%f  x=%f" % (n, p, mu, sigma, x)
        self.assertTrue(abs(x - mu) < 10.0 * sigma, msg)

    def check_probability(self, a, p):
        n = len(a)
        c = a.count()
        if p == 0:
            self.assertEqual(c, 0)
        elif p == 1:
            self.assertEqual(c, n)
        else:
            self.check_normal_dist(n, p, c)


class UtilTests(Util):

    def test_check_probability(self):
        N = 1_000_000

        self.check_probability(zeros(N), 0.0)
        self.check_probability(ones(N), 1.0)

        a = zeros(N)
        a[::2] = 1
        self.assertEqual(a.count(), N // 2)
        self.check_probability(a, 0.501)
        self.check_probability(a, 0.499)
        C = self.check_probability
        self.assertRaises(AssertionError, C, a, 0.506)
        self.assertRaises(AssertionError, C, a, 0.494)


class URandomTests(Util):

    def test_count(self):
        a = urandom(10_000_000)
        self.check_probability(a, 0.5)

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
                if n > 10 and p * (1.0 - p) > 0.01:
                    self.check_normal_dist(n, p, a.count())
                cum |= a
            self.assertEqual(len(cum), n)
            self.assertTrue(cum.all())

    def test_bits_evenly(self):
        n = 4000
        Nhalf = half = 0
        Neven = even = 0
        for _ in range(25_000):
            p = 1.5 * SMALL_P * random()
            a = random_p(n, p)
            tot = a.count()
            if p > 0.1:
                self.check_normal_dist(n, p, tot)

            c1 = a.count(1, 0, n // 2)  # bits in lower half
            c2 = a.count(1, n // 2, n)  #         upper
            self.assertEqual(c1 + c2, tot)
            if c1 != c2:
                Nhalf += 1
                if c1 > c2:
                    half += 1

            c1 = a.count(1, 0, n, 2)  # bits in even positions
            c2 = a.count(1, 1, n, 2)  #         odd
            self.assertEqual(c1 + c2, tot)
            if c1 != c2:
                Neven += 1
                if c1 > c2:
                    even += 1

        self.check_normal_dist(Nhalf, 0.5, half)
        self.check_normal_dist(Neven, 0.5, even)

    def test_elements_uniform(self):
        arrays = [random_p(100_000, 0.3) for _ in range(100)]
        for a in arrays:
            # for each bitarray see if population is within expectation
            self.check_probability(a, 0.3)

        c = count_each_index(arrays)
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

    def test_literal(self):
        # test "literal definition" case
        c = Counter(random_p(4, 0.25).count() for _ in range(100_000))
        self.assertTrue(abs(c[0] - 31_641) <= 1_471)
        self.assertTrue(abs(c[1] - 42_188) <= 1_562)
        self.assertTrue(abs(c[2] - 21_094) <= 1_290)
        self.assertEqual(c.total(), 100_000)

    def test_small_p(self):
        # test small p case
        c = Counter(random_p(50, p=0.005).count() for _ in range(100_000))
        self.assertTrue(abs(c[0] - 77_831) <= 1_314)
        self.assertTrue(abs(c[1] - 19_556) <= 1_254)
        self.assertEqual(c.total(), 100_000)

    def test_small_p_symmetry(self):
        # same as above - exploiting symmetry
        c = Counter(random_p(50, p=0.995).count() for _ in range(100_000))
        self.assertTrue(abs(c[49] - 19_556) <= 1_254)
        self.assertTrue(abs(c[50] - 77_831) <= 1_314)
        self.assertEqual(c.total(), 100_000)

    def test_small_p_uniform(self):
        c = count_each_index(random_p(100_000, 0.005) for _ in range(50))
        self.assertTrue(abs(c[0] - 77_831) <= 1_314)
        self.assertTrue(abs(c[1] - 19_556) <= 1_254)
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

    def test_operations(self):
        C = self.check_probability
        n = 1_000_000
        values = (0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0)

        for p in values:
            a = random_p(n, p)
            C(a, p)
            C(~a, 1 - p)

            for q in values:
                b = random_p(n, q)
                C(b, q)

                C(a & b, p * q)
                C(a | b, p + q - p * q)
                C(a ^ b, p + q - 2 * p * q)


if __name__ == '__main__':
    unittest.main(verbosity=2)
