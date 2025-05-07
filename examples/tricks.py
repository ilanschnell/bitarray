"""
This file contains some little tricks and verifications for some code which
is used in the C implementation of bitarray.
"""
from random import randint
import unittest


class TricksTests(unittest.TestCase):

    def test_range_check_simple(self):
        r = range(0, 256)
        for k in range(-10, 300):
            self.assertEqual(k < 0 or k > 0xff, bool(k >> 8))
            self.assertEqual(k not in r, bool(k >> 8))

    def test_range_check(self):
        # used in various places in C code
        for i in range(0, 11):
            m = 1 << i
            for k in range(-10, 2000):
                res1 = k not in range(0, m)
                res2 = k < 0 or k >= m
                self.assertEqual(res1, res2)
                # simply shift i to right and see if anything remains
                res3 = bool(k >> i)
                self.assertEqual(res1, res3)

    def test_range_check_2(self):
        # this is used in _util.c in set_count()
        for i in range(0, 11):
            m = 1 << i
            for k in range(-10, 2000):
                res1 = k not in range(0, m + 1)
                res2 = k < 0 or k > m
                self.assertEqual(res1, res2)
                # same as above but combined with k substracted by 1
                res3 = bool(k >> i) and bool((k - 1) >> i)
                self.assertEqual(res1, res3)

    def test_adjust_step_positive(self):
        for _ in range(10_000):
            start = randint(-100, 100)
            stop = randint(-100, 100)
            step = randint(-20, 20)
            if step == 0:
                continue
            r = range(start, stop, step)
            slicelength = len(r)

            if step < 0:
                stop = start + 1
                start = stop + step * (slicelength - 1) - 1
                step = -step
                r = r[::-1]

            self.assertEqual(range(start, stop, step), r)
            self.assertTrue(step > 0)
            if slicelength == 0:
                self.assertTrue(stop <= start)
            elif step == 1:
                self.assertEqual(stop - start, slicelength)

    def test_overlap(self):
        n = 200
        for _ in range(10_000):
            i1 = randint(0, n)
            j1 = randint(i1, n)
            r1 = range(i1, j1)

            i2 = randint(0, n)
            j2 = randint(i2, n)
            r2 = range(i2, j2)

            # test if ranges r1 and r2 overlap
            res1 = bool(r1) and bool(r2) and (i2 in r1 or i1 in r2)
            res2 = bool(set(r1) & set(r2))
            self.assertEqual(res1, res2)


if __name__ == '__main__':
    unittest.main()
