import unittest
from itertools import pairwise
from collections import Counter
from statistics import fmean
from random import sample


class RandomSampleTests(unittest.TestCase):

    def test_mean(self):
        # python sample.py 100_000 100 20 0 45.5 49.5 100
        M = 100_000  # number of trails
        N = 100      # population size
        K =  20      # sample size
        C = Counter()
        ranges = [0.0, 45.5, 49.5, 100.0]
        for _ in range(M):
            x = fmean(sample(range(N), K))
            for i, (x1, x2) in enumerate(pairwise(ranges)):
                if x1 <= x < x2:
                    C[i] += 1

        self.assertEqual(C.total(), M)
        self.assertTrue(abs(C[0] - 24_529) <=  1_361)  # p = 0.245291
        self.assertTrue(abs(C[1] - 25_471) <=  1_378)  # p = 0.254709
        self.assertTrue(abs(C[2] - 50_000) <=  1_581)  # p = 0.500000


if __name__ == '__main__':
    unittest.main()
