"""
Statistical Tests for Random Functions in bitarray.util
-------------------------------------------------------

These are statistical tests.  They do not test any basic functionality of
random functions.  Those are already tested in the regular utility tests.
Therefore, and because these tests take longer to run, we decided to put
them in a separate file.

In addition, this file contains some important verification tests that don't
test actual functionality in random_p(), but rather verify some of the logic
and establish some tricky equations.
"""
import sys
import unittest
from copy import deepcopy
from collections import Counter
from itertools import pairwise
from math import comb, fmod, sqrt
from statistics import fmean, stdev, pstdev
from random import choices, randint, randrange, random, binomialvariate

from bitarray import bitarray, frozenbitarray
from bitarray.util import (
    zeros, ones, urandom, random_k, random_p, sum_indices,
    int2ba, count_and, count_or, count_xor, parity,
)
from bitarray.util import _Random  # type: ignore


HEAVY = False   # set True for heavy testing


_r = _Random()
M = _r.M
K = _r.K
limit = 1.0 / (K + 1)  # lower limit for p
SMALL_P = _r.SMALL_P


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
            raise ValueError("bitarrays of same length expected")
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
        self.assertEqual(c.total(), 7)  # length of each bitarray
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


def create_masks(m):
    """
    Create a list with m masks.  Each mask has a length of 2**m bits.
    """
    masks = []
    for i in range(m):
        j = 1 << i
        mask = zeros(j) + ones(j)
        mask *= 1 << (m - i - 1)
        masks.append(mask)
    return masks


class CreateMasksTests(unittest.TestCase):

    def test_explict(self):
        C = create_masks
        self.assertEqual(C(0), [])

        self.assertEqual(C(1), [bitarray("01")])

        self.assertEqual(C(2), [bitarray("0101"),
                                bitarray("0011")])

        self.assertEqual(C(3), [bitarray("01010101"),
                                bitarray("00110011"),
                                bitarray("00001111")])

    def test_11(self):
        m = 11
        masks = create_masks(m)
        n = 1 << m
        self.assertEqual(len(masks), m)
        self.assertEqual(count_each_index(masks),
                         Counter(int2ba(i).count() for i in range(n)))
        for i in range(m):
            a = masks[i]
            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), n // 2)
            for j in range(i):
                b = masks[j]
                self.assertEqual(count_and(a, b), n // 4)
                self.assertEqual(count_or(a, b), 3 * n // 4)
                self.assertEqual(count_xor(a, b), n // 2)


class Util(unittest.TestCase):

    def check_binomial_dist(self, n, p, x):
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
            self.check_binomial_dist(n, p, c)


class UtilTests(Util):

    def test_check_probability(self):
        C = self.check_probability
        N = 1_000_000

        a = zeros(N)
        C(a, 0.0)

        a.setall(1)
        C(a, 1.0)

        a[::2] = 0
        self.assertEqual(a.count(), N // 2)
        C(a, 0.501)
        C(a, 0.499)
        self.assertRaises(AssertionError, C, a, 0.506)
        self.assertRaises(AssertionError, C, a, 0.494)


class URandomTests(Util):

    def test_count(self):
        a = urandom(10_000_000)
        self.check_probability(a, 0.5)

    def test_stat(self):
        for c in [
                Counter(urandom(100).count() for _ in range(100_000)),
                count_each_index(urandom(100_000) for _ in range(100)),
        ]:
            self.assertTrue(set(c) <= set(range(101)))
            self.assertEqual(c.total(), 100_000)
            x = sum(c[k] for k in range(40, 51))
            # p = 0.522195   mean = 52219.451858   stdev = 157.958033
            self.assertTrue(abs(x - 52_219) <= 1_580)


class Random_K_Tests(Util):

    def test_mean(self):
        M = 100_000  # number of trails
        N = 1_000    # bitarray length
        K = 500      # sample size
        C = Counter()
        ranges = [0.0, 500.0, 510.0, 520.0, 1000.0]
        for _ in range(M):
            x = sum_indices(random_k(N, K)) / K
            for i, (x1, x2) in enumerate(pairwise(ranges)):
                if x1 <= x < x2:
                    C[i] += 1

        self.assertEqual(C.total(), M)
        # python random/sample.py 100_000 1000 500 0 500 510 520 1000
        self.assertTrue(abs(C[0] - 52_183) <= 1_580)  # p = 0.521829
        self.assertTrue(abs(C[1] - 35_303) <= 1_511)  # p = 0.353025
        self.assertTrue(abs(C[2] - 11_275) <= 1_000)  # p = 0.112747
        self.assertTrue(abs(C[3] -  1_240) <=   350)  # p = 0.012399

    def test_mean_2(self):
        M = 100_000  # number of trails
        N = 500      # bitarray length
        K = 400      # sample size
        C = Counter()
        ranges = [200.0, 249.5, 251.0, 255.0, 260.0, 300.0]
        for _ in range(M):
            x = sum_indices(random_k(N, K)) / K
            for i, (x1, x2) in enumerate(pairwise(ranges)):
                if x1 <= x < x2:
                    C[i] += 1

        self.assertEqual(C.total(), M)
        # python random/sample.py 100_000 500 400 200 249.5 251 255 260 300
        self.assertTrue(abs(C[0] - 50_000) <= 1_581)  # p = 0.500000
        self.assertTrue(abs(C[1] - 17_878) <= 1_212)  # p = 0.178781
        self.assertTrue(abs(C[2] - 27_688) <= 1_415)  # p = 0.276879
        self.assertTrue(abs(C[3] -  4_376) <=   647)  # p = 0.043762

    def test_apply_masks(self):
        Na = 25_000  # number of bitarrays to test against masks
        Nm = 12      # number of masks
        n = 1 << Nm  # length of each mask
        # Create masks for selecting half elements in random bitarray a.
        # For example, masks[0] selects all odd elements, and masks[-1]
        # selects the upper half of a.
        masks = create_masks(Nm)
        cm = Nm * [0]  # counter for each mask
        for _ in range(Na):
            k = randrange(1, n, 2)  # k is odd
            a = random_k(n, k)
            self.assertEqual(len(a), n)
            self.assertTrue(parity(a))  # count is odd
            for i in range(Nm):
                c1 = count_and(a, masks[i])
                c0 = k - c1
                # counts cannot be equal because k is odd
                self.assertNotEqual(c0, c1)
                # the probability for having more, e.g. even than
                # odd (masks[0]) elements should be 1/2, or having more bits
                # in upper vs lower half (mask(-1))
                if c0 > c1:
                    cm[i] += 1

        for c in cm:  # for each mask, check counter
            self.check_binomial_dist(Na, 0.5, c)

    def test_random_masks(self):
        Na = 10  # number of arrays to test
        Nm = 500_000 if HEAVY else 25_000  # number of masks
        n = 7000  # bitarray length
        # count for each array
        ka = choices(range(1, n, 2), k=Na)
        arrays = [random_k(n, k) for k in ka]

        for k, a in zip(ka, arrays):  # sanity check arrays
            self.assertEqual(len(a), n)
            self.assertEqual(a.count(), k)
            self.assertTrue(parity(a))

        ca = Na * [0]  # counter for each array
        for _ in range(Nm):
            # each mask has exactly half elements set to 1
            mask = random_k(n, n//2)
            self.assertEqual(mask.count(0), mask.count(1))

            # test each array against this masks
            for i in range(Na):
                c1 = count_and(arrays[i], mask)
                c0 = ka[i] - c1
                # counts cannot be equal because k is odd
                self.assertNotEqual(c0, c1)
                if c0 > c1:
                    ca[i] += 1

        for c in ca:  # for each array, check counter
            self.check_binomial_dist(Nm, 0.5, c)

    def test_elements_uniform(self):
        arrays = [random_k(100_000, 30_000) for _ in range(100)]
        for a in arrays:
            # for each bitarray check sample size k
            self.assertEqual(a.count(), 30_000)

        c = count_each_index(arrays)
        self.assertTrue(abs(c[30] - 8_678) <= 890)
        x = sum(c[k] for k in range(20, 31))
        # p = 0.540236   mean = 54023.639245   stdev = 157.601089
        self.assertTrue(abs(x - 54_024) <= 1_576)
        self.assertEqual(c.total(), 100_000)

    def test_all_bits_active(self):
        for _ in range(100):
            n = randrange(10, 10_000)
            cum = zeros(n)
            for _ in range(10_000):
                k = n // 7
                a = random_k(n, k)
                self.assertEqual(len(a), n)
                self.assertEqual(a.count(), k)
                cum |= a
                if cum.all():
                    break
            else:
                self.fail()

    def test_combinations(self):
        # for entire range of 0 <= k <= n, validate that random_k()
        # generates all possible combinations
        n = 12
        total = 0
        for k in range(n + 1):
            expected = comb(n, k)
            combs = set()
            for _ in range(100_000):
                a = random_k(n, k)
                self.assertEqual(a.count(), k)
                combs.add(frozenbitarray(a))
                if len(combs) == expected:
                    total += expected
                    break
            else:
                self.fail()
        self.assertEqual(total, 2 ** n)

    def test_evenly(self):
        # Calculate random_k(n, k) N times, and count each specific outcome.
        # We know that there are m=comb(n, k) possible outcomes, so each one
        # has a probability 1/m and the mean of the count should be N/m.
        N = 100_000
        n = 9
        k = 3
        m = comb(n, k)
        c = Counter()
        for _ in range(N):
            a = frozenbitarray(random_k(n, k))
            c[a] += 1
        self.assertEqual(c.total(), N)
        self.assertEqual(len(c), m)
        p = 1.0 / m
        self.assertAlmostEqual(fmean(c.values()), N * p)
        if 0:
            print(m)
            print(N * p)
            print(sqrt(N * p * (1.0 - p)))
            print(stdev(c.values()))
        for x in c.values():
            self.check_binomial_dist(N, p, x)

    def random_p_alt(self, n, p=0.5):
        """
        Alternative implementation of random_p().  While the performance is
        about the same for large n, we found that for smaller n the handling
        of special cases leads to better overall performance in the current
        implementation.
        """
        k = binomialvariate(n, p)
        self.assertTrue(0 <= k <= n)
        a = random_k(n, k)
        self.assertEqual(len(a), n)
        self.assertEqual(a.count(), k)
        return a

    def test_random_p_alt(self):
        n = 1_000_000
        for _ in range(100):
            p = random()
            a = self.random_p_alt(n, p)
            self.check_probability(a, p)


class Random_P_Tests(Util):

    def test_apply_masks(self):
        M = 12  # number of masks
        # Create masks for selecting half elements in the random bitarray a.
        # For example, masks[0] selects all odd elements, and masks[-1]
        # selects the upper half of a.
        masks = create_masks(M)
        n = M * [0]  # sample size for each mask
        c = M * [0]  # count for each mask
        for _ in range(25_000):
            p = 1.5 * SMALL_P * random()
            a = random_p(1 << M, p)
            tot = a.count()
            for i in range(M):
                c1 = count_and(a, masks[i])
                c0 = tot - c1
                if c0 == c1:  # counts are equal ->
                    continue  # ignore this mask for this bitarray a
                n[i] += 1
                # counts are not equal, the probability for having more,
                # e.g. even than odd (masks[0]) elements should be 1/2,
                # or having more bits in upper vs lower half (mask(-1))
                if c0 > c1:
                    c[i] += 1

        for i in range(M):
            self.assertTrue(n[i] > 20_000, n[i])
            self.check_binomial_dist(n[i], 0.5, c[i])

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
        # test "literal definition" case, n = 5
        M = 250_000  # number of trails
        C = Counter(random_p(5, 0.3).count() for _ in range(M))
        self.assertEqual(C.total(), M)
        # python random/binomial.py 250_000 5 0.3
        self.assertTrue(abs(C[0] - 42_017) <=  1_870)  # p = 0.168070
        self.assertTrue(abs(C[1] - 90_037) <=  2_400)  # p = 0.360150
        self.assertTrue(abs(C[2] - 77_175) <=  2_310)  # p = 0.308700
        self.assertTrue(abs(C[3] - 33_075) <=  1_694)  # p = 0.132300
        self.assertTrue(abs(C[4] -  7_087) <=    830)  # p = 0.028350

    def test_small_p(self):
        # test small p case
        C = Counter(random_p(50, p=0.005).count() for _ in range(100_000))
        self.assertEqual(C.total(), 100_000)
        # python random/binomial.py 100_000 50 .005
        self.assertTrue(abs(C[0] - 77_831) <=  1_314)  # p = 0.778313
        self.assertTrue(abs(C[1] - 19_556) <=  1_254)  # p = 0.195556

    def test_small_p_symmetry(self):
        # same as above - exploiting symmetry
        C = Counter(random_p(50, p=0.995).count() for _ in range(100_000))
        self.assertEqual(C.total(), 100_000)
        self.assertTrue(abs(C[49] - 19_556) <= 1_254)
        self.assertTrue(abs(C[50] - 77_831) <= 1_314)

    def test_small_p_uniform(self):
        C = count_each_index(random_p(100_000, 0.005) for _ in range(50))
        self.assertEqual(C.total(), 100_000)
        self.assertTrue(abs(C[0] - 77_831) <= 1_314)
        self.assertTrue(abs(C[1] - 19_556) <= 1_254)

    def test_p375(self):
        # test .combine_half()
        M = 100_000  # number of trails
        C = Counter(random_p(100, 0.375).count() for _ in range(M))
        self.assertEqual(C.total(), M)
        # python random/binomial.py 100_000 100 .375 37..48
        self.assertTrue(abs(C[36] -  7_898) <=    853)  # p = 0.078977
        self.assertTrue(abs(C[37] -  8_196) <=    867)  # p = 0.081965
        self.assertTrue(abs(C[38] -  8_153) <=    865)  # p = 0.081533
        self.assertTrue(abs(C[39] -  7_777) <=    847)  # p = 0.077770
        x = sum(C[k] for k in range(37, 49))
        self.assertTrue(abs(x - 56_614) <=  1_567)  # p = 0.566139

    def test_ne5(self):
        M = 25_000  # number of trails
        C = Counter(random_p(100_000, 0.5).count() for _ in range(M))
        self.assertEqual(C.total(), M)
        # python binomial.py 25_000 100_000 .5 48_000..50_000 50_000..50_200
        x = sum(C[k] for k in range(48000, 50001))
        self.assertTrue(abs(x - 12_532) <=    791)  # p = 0.501262
        x = sum(C[k] for k in range(50000, 50201))
        self.assertTrue(abs(x -  9_972) <=    774)  # p = 0.398876

    def test_probabilities(self):
        n = 100_000_000
        special_p = [
            65 / 257 - 1e-9,  # largest x for OR
            65 / 257 + 1e-9,  # smallest x for AND
            0.0, 1e-12, 0.25, 1/3, 3/8, 127/257, 0.5,
        ]
        for j in range(100 if HEAVY else 2):
            sys.stdout.write('.')
            sys.stdout.flush()
            try:
                p = special_p[j]
            except IndexError:
                p = random()

            a = random_p(n, p)
            self.check_probability(a, p)


class VerificationTests(Util):

    def test_uniform_stdev(self):
        # verify that the standard deviation of a uniform distribution
        # of population size n is given by: n / sqrt(12)
        for _ in range(100):
            n = randrange(10, 10_000)
            pop = list(range(n))
            self.assertEqual(fmean(pop), (n - 1) / 2)
            self.assertAlmostEqual(pstdev(pop), n / sqrt(12), delta=0.1)

    def test_operations(self):
        C = self.check_probability
        n = 1_000_000
        values = [i / 16.0 for i in range(17)]
        arrays0, arrays1 = ([(random_p(n, p), p) for p in values]
                            for _ in range(2))

        for a, p in arrays0:
            C(a, p)
            C(~a, 1.0 - p)                   # invert

            for b, q in arrays1:
                C(b, q)

                C(a & b, p * q)              # AND
                C(a | b, p + q - p * q)      # OR
                C(a ^ b, p + q - 2 * p * q)  # XOR

        for b, q in arrays0:
            C(b, q)
            for a, p in deepcopy(arrays1):
                C(a, p)

                a &= b                       # in-place AND
                p *= q
                C(a, p)

            for a, p in deepcopy(arrays1):
                C(a, p)

                a |= b                       # in-place OR
                p += q * (1.0 - p)
                C(a, p)

            for a, p in deepcopy(arrays1):
                C(a, p)

                a ^= b                       # in-place XOR
                p += q * (1.0 - 2 * p)
                C(a, p)

    # ---------------- verifications relevant for random_k() ----------------

    def test_decide_on_sequence(self):
        N = 100_000
        cdiff = Counter()

        for _ in range(N):
            n = randrange(1, 10_000)
            k = randint(0, n // 2)
            self.assertTrue(0 <= k <= n // 2)

            if k < 16 or k * K < 3 * n:
                # for small k, we increase the count of a zeros(n) bitarray
                i = 0
            else:
                # We could simply have `i = int(k / n * K)`.  However,
                # when k is small, many reselections are required to
                # decrease the count.  On the other hand, for k near n/2,
                # increasing and decreasing the count is equally expensive.
                p = k / n  # p <= 0.5
                # Numerator: f(p)=(1-2*p)*c  ->  f(0)=c, f(1/2)=0
                # As the standard deviation of the .combine_half() bitarrays
                # gets smaller with larger n, we divide by sqrt(n).
                p -= (0.2 - 0.4 * p) / sqrt(n)
                # Note that we divide by K+1.  This will round towards the
                # nearest probability as we get closer to p = 1/2.
                i = int(p * (K + 1))

            if i < 3:
                # a = zeros(n), count is 0
                diff = -k
            else:
                self.assertTrue(k >= 16)
                self.assertTrue(n >= 32)
                self.assertTrue(3 <= i <= K // 2)
                # a = self.combine_half(self.op_seq(i))
                # count is given by binomialvariate(n, i / K)
                diff = binomialvariate(n, i / K) - k

            cdiff[diff] += 1

        self.assertEqual(cdiff.total(), N)
        # count the number of cases where the count needs to be decreased
        above = sum(cdiff[i] for i in range(1, max(cdiff) + 1))
        self.assertTrue(M != 8 or 0.28 < above / N < 0.34)

    # ---------------- verifications relevant for random_p() ----------------

    def test_equal_x(self):
        """
        Verify that the probabilities p for which final AND and OR result in
        equal x are:  p = j / (K + 1)    j in range(1, K)
        Also, verify these x are all:  x = 1 / (K + 1) = limit
        These are also the maximal x.
        """
        for j in range(1, K):
            # probabilities p for which final AND and OR result in equal x
            p = j / (K + 1)
            i = int(p * K)
            self.assertEqual(i, j - 1)  # as K / (K + 1) < 1
            self.assertEqual(p * (K + 1), i + 1)
            q = i / K
            x1 = (p - q) / (1.0 - q)      # OR
            x2 = 1.0 - p / (q + 1.0 / K)  # AND   x2 = 1 - p / next q
            self.assertAlmostEqual(x1, x2, delta=1e-14)
            self.assertAlmostEqual(x1, limit, delta=1e-14)

    def special_p(self):
        """
        generate special test values of p < 0.5
        """
        EPS = 1e-12

        for j in range(1, K // 2 + 1):
            # probabilities for which final AND and OR result in equal x
            p = j / (K + 1)
            for e in -EPS, EPS:
                yield p + e

        for j in range(1, K // 2):
            # probabilities for which no final AND or OR is not necessary
            p = j / K
            for e in -EPS, 0.0, EPS:
                yield p + e

        for p in 0.0, EPS, 0.5 - EPS:
            yield p

        for e in -EPS, 0.0, EPS:
            yield SMALL_P + e

        for _ in range(10_000):
            yield 0.5 * random()

    def test_decide_on_operation(self):
        """
        Verify that `x1 > x2` equates to `p * (K + 1) > i + 1`.
        """
        for p in self.special_p():
            self.assertTrue(0 <= p < 0.5, p)

            i = int(p * K)
            q = i / K
            self.assertTrue(q <= p)
            x1 = (p - q) / (1.0 - q)      # OR
            x2 = 1.0 - p / (q + 1.0 / K)  # AND   x2 = 1 - p / next q
            # decided whether to use next i (next q)
            self.assertEqual(x1 > x2,
                             p * (K + 1) > i + 1)

    def test_decision_limit(self):
        """
        Verify that decision operation works as desired, and that resulting
        probability q is within limit of p.
        """
        # limit = 1/(K+1) is slightly smaller than 1/K:
        self.assertEqual(limit, 1.0 / K - 1.0 / (K * (K + 1)))
        self.assertTrue(1.0 / K - limit < K ** -2 == 1.0 / (1 << (2 * M)))

        for p in self.special_p():
            i = int(p * K)
            q0 = i / K
            q1 = (i + 1) / K
            self.assertTrue(q0 <= p < q1)
            self.assertTrue(q1 - q0 == 1.0 / K > limit)
            self.assertTrue(q0 + 0.5 * limit < q1 - 0.5 * limit)

            if p * (K + 1) > i + 1:
                self.assertTrue(q1 - 0.5 * limit < p < q1)
                # implies:
                self.assertNotEqual(q0, p)
                q = q1
                self.assertTrue(q > p)      # use AND operation
            else:
                self.assertTrue(q0 <= p < q0 + limit)
                q = q0
                self.assertTrue(q <= p)     # use OR operation

            self.assertTrue(p - limit < q < p + 0.5 * limit)
            self.assertTrue(abs(p - q) < limit)
            self.assertEqual(bool(q != p), bool(fmod(p, 1.0 / K)))

    def test_final_op(self):
        """
        Verify final operation always gives us the correct probability.
        """
        for p in self.special_p():
            i = int(p * K)
            if p * (K + 1) > i + 1:  # see above
                i += 1

            if p > limit:
                self.assertNotEqual(i, 0)
                # Note that all the below handles this case fine.
                # However, rather than extending .op_seq() and .combine_half()
                # to handle i=0, we decided to "filter out" i=0 by the small p
                # case (see test below).
            self.assertTrue(0 <= i <= K // 2)

            q = i / K
            self.assertTrue(abs(p - q) < limit)  # see above

            if q < p:  # increase probability - OR
                x = (p - q) / (1.0 - q)
                # ensure small p case is called
                self.assertTrue(0.0 < x < limit)
                q += x * (1.0 - q)   # OR
            elif q > p:  # decrease probability - AND
                x = p / q
                # ensure small p case is called (after symmetry is exploited)
                self.assertTrue(0.0 < 1.0 - x < limit)
                q *= x               # AND
            self.assertEqual(q, p)

    def test_i_not_0(self):
        """
        Verify that for `p > limit`, we always get `i > 0`.
        This is important, as the small p case has to "filter out" `i = 0`,
        as the sequence of operations do not handle `i = 0`.
        """
        p = limit + 1e-12
        i = int(p * K)
        self.assertEqual(i, 0)  # as K / (K + 1) < 1
        if p * (K + 1) > i + 1:
            i += 1
        # So for i be non-zero we must have:
        #     p * (K + 1) > 1
        # or
        #     p > 1 / (K + 1) = limit        q.e.d.
        self.assertEqual(i, 1)

    def dummy_random_p(self, p=0.5, verbose=False):
        """
        Unlike random_p(), this function returns the desired probability q
        itself, and not a random bitarray.  The point of this function is to
        illustrate how random_p() essentially works.
        Instead of actual bitarray operations, we change q accordingly.
        This method is neither concerned with the bitarray length n nor
        endianness.
        """
        # error check inputs and handle edge cases
        if p <= 0.0 or p == 0.5 or p >= 1.0:
            if p in (0.0, 0.5, 1.0):
                return p
            raise ValueError("p must be in range 0.0 <= p <= 1.0, got %f", p)

        # exploit symmetry to establish: p < 0.5
        if p > 0.5:
            return 1.0 - self.dummy_random_p(1.0 - p, verbose)

        # for small p set randomly individual bits, which is much faster
        if p < SMALL_P:
            return p  # random.binomialvariate() and .random_pop()

        # calculate operator sequence
        i = int(p * K)
        if p * (K + 1) > i + 1:
            i += 1
        self.assertTrue(0 < i <= K // 2)
        a = bitarray(i.to_bytes(2, byteorder="little"), "little")
        seq = a[a.index(1) + 1 : M]

        # combine random bitarrays using bitwise AND and OR operations
        q = 0.5  # start with randbytes()
        for k in seq:
            if k:
                q += 0.5 * (1.0 - q)  # OR
            else:
                q *= 0.5              # AND
        self.assertEqual(q, i / K)

        x = 0.0
        if q < p:  # increase probability
            x = (p - q) / (1.0 - q)
            self.assertTrue(0.0 < x < SMALL_P)
            q += x * (1.0 - q)        # OR
        elif q > p:  # decrease probability
            x = p / q
            self.assertTrue(0.0 < 1.0 - x < SMALL_P)
            q *= x                    # AND

        if verbose:
            print("%15.9f  %9d  %9d  %15.9f" % (p, len(seq) + 1, i, x))
        self.assertEqual(q, p)
        return q

    def test_dummy_random_p(self):
        for p in self.special_p():
            self.assertEqual(self.dummy_random_p(p), p)

        # test 0 <= p < 1; self.special_p() only gives us 0 <= p < 0.5
        for _ in range(10_000):
            p = random()
            self.assertEqual(self.dummy_random_p(p), p)

def disp():
    i = sys.argv.index('--disp')
    args = sys.argv[i + 1:]
    if args:
        plist = [float(eval(s)) for s in args]
    else:
        plist = [1/4, 1/8, 1/16, 1/32, 1/64, 3/128, 127/256,
                 SMALL_P, 0.1, 0.2, 0.3, 0.4,
                 65/257, 127/257 + 1e-9, 0.5 - 1e-9]
    print("      p                  k          i        x")
    print(55 * '-')
    for p in plist:
        VerificationTests().dummy_random_p(p, True)


if __name__ == '__main__':
    if '--disp' in sys.argv:
        disp()
        sys.exit()
    if "--heavy" in sys.argv:
        HEAVY = True
        sys.argv.remove("--heavy")
    unittest.main()
