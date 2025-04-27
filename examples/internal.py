"""
This file contains some tricks and verifications for some code which is
used in the C implementation of bitarray.
"""
from random import randint
import unittest


class InternalTests(unittest.TestCase):

    def test_range_check(self):
        # used in various places in C code
        for i in range(0, 11):
            for k in range(-10, 2000):
                m = 1 << i
                res1 = k not in range(0, m)
                res2 = k < 0 or k >= m
                self.assertEqual(res1, res2)
                # simply shift i to right and see if anything remains
                res3 = bool(k >> i)
                self.assertEqual(res1, res3)

    def test_range_check_2(self):
        # this is used in _util.c in set_count()
        for i in range(0, 11):
            for k in range(-10, 2000):
                m = 1 << i
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


if __name__ == '__main__':
    unittest.main()
