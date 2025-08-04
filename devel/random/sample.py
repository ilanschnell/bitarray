import sys
from math import sqrt, erf, exp, pi


class SampleMeanDist:

    # This class describes the mean of a discrete uniform distribution
    # without replacement of k integers in range(n).
    # That is, from the integers in range(n) (population), we take a random
    # sample of k (without replacement) and calculate their mean value x.
    # The distribution of x is what we are interested it.

    def __init__(self, n, k):
        assert n >= 1
        assert 1 <= k < n
        self.n = n
        self.k = k

        # the sample mean is the population mean
        self.mu = 0.5 * (n - 1)  # sum(range(n)) / n = (n - 1) / 2

        # The standard deviation of a sample, is also called standard error.
        # The standard error of the mean is the standard deviation of the
        # population, divided by the square root of the sample size k.
        # The variance of the population is:
        #
        #     (n**2 - 1) / 12     (see below)
        #
        # So for the standard deviation, we get:
        self.sigma = sqrt((n * n - 1) / (12 * k))

        # Finite population correction (FPC)
        # ----------------------------------
        #
        # Let us consider two cases:
        #
        # (a) For very small sample sizes k (compared to the population
        #     size n), the FPC is close to one.  That is, it has no effect
        #     on the standard error.  Our distribution is basically the same
        #     as if we had replacement.
        #
        # (b) For sample sizes k close to the population size n, the FPC (and
        #     hence the standard error) becomes zero.  That is, when we
        #     sample all elements, we always get the same sample mean (the
        #     population mean) with no standard error.
        #
        fpc = sqrt((n - k) / (n - 1))
        self.sigma *= fpc

    def print(self):
        print("n = %d    k = %d" % (self.n, self.k))
        print("mu = %f" % self.mu)
        print("sigma = %f" % self.sigma)

    def pdf(self, x):
        return exp(-0.5 * ((x - self.mu) / self.sigma) ** 2) / (
            sqrt(2.0 * pi) * self.sigma)

    def cdf(self, x):
        return 0.5 * (1.0 + erf((x - self.mu) / (self.sigma * sqrt(2.0))))

    def range_p(self, x1, x2):
        "probability for x (the mean of the sample) being in x1 < x < x2"
        assert 0 <= x1 <= x2 <= self.n
        return self.cdf(x2) - self.cdf(x1)

# ---------------------------------------------------------------------------

import unittest

class SampleMeanDistTests(unittest.TestCase):

    def test_pdf(self):
        n, k = 100, 30
        smd = SampleMeanDist(n, k)
        N = 1000
        dx = n / N
        p = 0.0
        for i in range(N + 1):
            x = i * dx
            p += smd.pdf(x) * dx
        self.assertAlmostEqual(p, 1.0)

    def test_cdf(self):
        smd = SampleMeanDist(100, 30)
        self.assertAlmostEqual(smd.cdf(  0.0), 0.0)
        self.assertAlmostEqual(smd.cdf( 49.5), 0.5)
        self.assertAlmostEqual(smd.cdf(100.0), 1.0)

    def test_range_half(self):
        for k in 10, 20, 50, 100, 150:
            smd = SampleMeanDist(200, k)
            self.assertAlmostEqual(smd.range_p(0.0, 99.5), 0.5)
            self.assertAlmostEqual(smd.range_p(99.5, 200), 0.5)

    def test_verify_pop_mean(self):
        for n in range(1, 100):
            self.assertEqual((n - 1) /2, sum(range(n)) / n)

    def test_verify_pop_variance(self):
        for n in range(1, 100):
            mean = (n - 1) / 2
            sigma2 = sum((j - mean)**2 for j in range(n)) / n
            self.assertEqual((n * n - 1) / 12, sigma2)


if __name__ == '__main__':
    if len(sys.argv) == 1:
        unittest.main()

    # This code was used to create some of the tests for util.random_k()
    # in ../test_random.py
    #
    # python sample.py 100_000 1000 500 0 500 510 520 1000
    # python sample.py 100_000 500 400 200 249.5 251 255 260 300

    from itertools import pairwise

    m, n, k = [int(i) for i in sys.argv[1:4]]
    smd = SampleMeanDist(n, k)
    smd.print()
    ranges = [float(x) for x in sys.argv[4:]]
    print("ranges = %r" % ranges)
    p_tot = 0.0
    for i, (x1, x2) in enumerate(pairwise(ranges)):
        p = smd.range_p(x1, x2)
        p_tot += p
        mu = m * p
        sigma = sqrt(m * p * (1.0 - p))
        if mu < 10 * sigma:
            continue
        fmt = "self.assertTrue(abs(C[{:d}] - {:6_d}) <= {:6_d})  # p = {:f}"
        print(fmt.format(i, round(mu), round(10 * sigma), p))

    if abs(p_tot - 1.0) > 1e-15:
        print(p_tot)
