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
import math
import unittest
from copy import deepcopy
from collections import Counter
from random import randrange, random

from bitarray import bitarray
from bitarray.util import (
    zeros, ones, urandom, random_p, int2ba, count_and, count_or, count_xor
)
from bitarray.util import _RandomP  # type: ignore


HEAVY = False   # set True for heavy testing


_r = _RandomP()
M = _r.M
K = _r.K
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

    def check_normal_dist(self, n, p, x):
        mu = n * p
        sigma = math.sqrt(n * p * (1.0 - p))
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
        for c in [
                Counter(urandom(100).count() for _ in range(100_000)),
                count_each_index(urandom(100_000) for _ in range(100)),
        ]:
            self.assertTrue(set(c) <= set(range(101)))
            self.assertEqual(c.total(), 100_000)
            x = sum(c[k] for k in range(40, 51))
            # p = 0.522195   mean = 52219.451858   stdev = 157.958033
            self.assertTrue(abs(x - 52_219) <= 1_580)


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

    def test_apply_masks(self):
        M = 12
        # Create masks for selecting half elements in the random bitarray a.
        # For example, masks[0] selects y'all odd elements, and masks[-1]
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
            self.check_normal_dist(n[i], 0.5, c[i])

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

    def test_equal_x(self):
        """
        Verify that the probabilities p for which final AND and OR result in
        equal x are:  p = j / (K + 1)    j in range(1, K)
        Also, verify these x are all:  x = 1 / (K + 1)
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
            self.assertAlmostEqual(x1, x2)
            self.assertAlmostEqual(x1, 1.0 / (K + 1))

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
            # decided whether to use next i (level of q)
            self.assertEqual(x1 > x2,
                             p * (K + 1) > i + 1)
            # note that the latter implies q != p
            if p * (K + 1) > i + 1:
                self.assertNotEqual(q, p)

    def test_final_op(self):
        """
        Verify final operation always gives us the correct probability,
        and establish lower limit for p.
        """
        limit = 1.0 / (K + 1)  # lower limit for p

        for p in self.special_p():
            i = int(p * K)
            self.assertTrue(i / K <= p)
            if p * (K + 1) > i + 1:  # see above
                i += 1
                self.assertTrue(i / K > p)

            if p > limit:  # see below
                self.assertNotEqual(i, 0)
            self.assertTrue(i <= K // 2)

            q = i / K
            self.assertTrue(abs(p - q) < limit)
            self.assertEqual(bool(q != p), bool(math.fmod(p, 1.0 / K)))

            if q < p:
                x = (p - q) / (1.0 - q)
                # ensure small p case is called
                self.assertTrue(0.0 < x < limit)
                q += x * (1.0 - q)   # OR
            elif q > p:
                x = p / q
                # ensure small p case is called (after symmetry is exploited)
                self.assertTrue(0.0 < 1.0 - x < limit)
                q *= x               # AND
            self.assertEqual(q, p)

    def test_i_not_0(self):
        """
        Verify that `p > limit` filters out i = 0 for .op_seq().
        """
        EPS = 1e-12
        limit = 1.0 / (K + 1)  # lower limit for p

        for e, res in [
                (-EPS, 0),
                ( 0.0, 0),
                (+EPS, 1),
        ]:
            p = limit + e
            i = int(p * K)
            self.assertEqual(i, 0)
            if p * (K + 1) > i + 1:
                i += 1
            # So for i not be zero we must have:
            #     p * (K + 1) > 1
            # or
            #     p > 1 / (K + 1) = limit        q.e.d.
            self.assertEqual(i, res)

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
            raise ValueError("p must be in range 0.0 <= p <= 1.0")

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
        if q < p:
            x = (p - q) / (1.0 - q)
            self.assertTrue(0.0 < x < SMALL_P)
            q += x * (1.0 - q)        # OR
        elif q > p:
            x = p / q
            self.assertTrue(0.0 < 1.0 - x < SMALL_P)
            q *= x                    # AND

        if verbose:
            print("%15.9f  %9d  %15.9f" % (p, len(seq) + 1, x))
        self.assertEqual(q, p)
        return q

    def test_dummy_random_p(self):
        for p in self.special_p():
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
