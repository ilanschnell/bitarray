import sys
from math import sqrt

from scipy.special import betainc


class BinomialDist:

    # This class describes the binomial distribution with parameters n and p.
    # That is, the (discrete) probability distribution of the number of
    # successes in a sequence of n independent Bernoulli trails, with each
    # trail having a probability p of success.

    def __init__(self, n, p):
        assert n > 0
        assert 0.0 <= p <= 1.0
        self.n = n
        self.p = p
        self.q = 1.0 - p
        self.mu = n * p
        self.sigma = sqrt(n * p * self.q)

    def print(self):
        print("n = %d    p = %f" % (self.n, self.p))
        print("mu = %f" % self.mu)
        print("sigma = %f" % self.sigma)

    def pmf(self, k):
        assert 0 <= k <= self.n, k
        # The reason we use .cdf() to calculate the PMF is because
        # comb(n, k) * p ** k  *  (1.0 - p) ** (n - k)  will fail for large
        # n, whereas .cdf() uses the regularized incomplete beta function.
        return self.cdf(k) - self.cdf(k - 1)

    def cdf(self, k):
        return betainc(self.n - k, k + 1, self.q)

    def range_k(self, k1, k2):
        "probability for k being in k1 <= k <= k2"
        assert 0 <= k1 <= k2 <= self.n
        return self.cdf(k2) - self.cdf(k1 - 1)

# ---------------------------------------------------------------------------

import unittest
from math import comb

class BinomialDistTests(unittest.TestCase):

    def test_pmf_simple(self):
        b = BinomialDist(1, 0.7)
        self.assertAlmostEqual(b.pmf(0), 0.3)
        self.assertAlmostEqual(b.pmf(1), 0.7)

        b = BinomialDist(2, 0.5)
        self.assertAlmostEqual(b.pmf(0), 0.25)
        self.assertAlmostEqual(b.pmf(1), 0.50)
        self.assertAlmostEqual(b.pmf(2), 0.25)

    def test_pmf_sum(self):
        for n in 10, 100, 1_000, 10_000:
            b = BinomialDist(n, 0.5)
            tot = 0
            for k in range(n + 1):
                tot += b.pmf(k)
            self.assertAlmostEqual(tot, 1.0)

    def test_pmf(self):
        for n in 10, 50, 100, 250:
            for p in 0.1, 0.2, 0.5, 0.7:
                b = BinomialDist(n, p)
                for k in range(n + 1):
                    res = comb(n, k) * p ** k * (1.0 - p) ** (n - k)
                    self.assertAlmostEqual(b.pmf(k), res, delta=1e-14)

    def test_cdf(self):
        for n in 5, 50, 500:
            b = BinomialDist(n, 0.3)
            self.assertAlmostEqual(b.cdf(-1), 0.0)
            self.assertAlmostEqual(b.cdf(n), 1.0)
            sm = 0.0
            for k in range(n + 1):
                sm += b.pmf(k)
                self.assertAlmostEqual(b.cdf(k), sm)

    def test_range_k(self):
        n = 10_000
        for p in 0.1, 0.2, 0.5, 0.7:
            b = BinomialDist(n, p)
            self.assertAlmostEqual(b.range_k(0, n), 1.0)
            for k in range(n + 1):
                self.assertAlmostEqual(b.range_k(k, k), b.pmf(k))

    def test_range_half(self):
        n = 1_000_001
        b = BinomialDist(n, 0.5)
        self.assertAlmostEqual(b.range_k(0, 500_000), 0.5)
        self.assertAlmostEqual(b.range_k(500_001, n), 0.5)


if __name__ == '__main__':
    if len(sys.argv) == 1:
        unittest.main()

    # This code was used to create some of the tests for util.random_p()
    # in ../test_random.py
    #
    # python binomial.py 250_000 5 0.3
    # python binomial.py 100_000 100 .375 37..48
    # python binomial.py 25_000 100_000 .5 48_000..50_000 50_000..50_200

    m, n = [int(i) for i in sys.argv[1:3]]
    p = float(sys.argv[3])
    bd = BinomialDist(n, p)
    bd.print()
    if n <= 100_000:
        p_tot = 0.0
        for k in range(n + 1):
            p = bd.pmf(k)
            p_tot += p
            if p < 0.01:
                continue
            bb = BinomialDist(m, p)
            fmt = "self.assertTrue(abs(C[{:d}] - {:6_d}) <= {:6_d})  # p = {:f}"
            print(fmt.format(k, round(bb.mu), round(10 * bb.sigma), p))
        assert abs(p_tot - 1.0) < 1e-15, p_tot

    for s in sys.argv[4:]:
        k1, k2 = [int(t) for t in s.split("..")]
        p = bd.range_k(k1, k2)
        bb = BinomialDist(m, p)
        print("x = sum(C[k] for k in %s)" % range(k1, k2 + 1))
        fmt = "self.assertTrue(abs(x - {:6_d}) <= {:6_d})  # p = {:f}"
        print(fmt.format(round(bb.mu), round(10 * bb.sigma), p))
