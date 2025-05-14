"""
This file contains some little tricks and verifications for some code which
is used in the C implementation of bitarray.
"""
import sys
from random import randint, randrange
import unittest


# ---------------------------- Range checks ---------------------------------

class RangeTests(unittest.TestCase):

    def test_check_simple(self):
        r = range(0, 256)
        for k in range(-10, 300):
            self.assertEqual(k < 0 or k > 0xff, bool(k >> 8))
            self.assertEqual(k not in r, bool(k >> 8))

    def test_check(self):
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

    def test_check_2(self):
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

# ------------------------------ Slicing ------------------------------------

def adjust_step_positive(slicelength, start, stop, step):
    """
    This is the adjust_step_positive() implementation from bitarray.h.
    """
    if step < 0:
        stop = start + 1
        start = stop + step * (slicelength - 1) - 1
        step = -step

    assert start >= 0 and stop >= 0
    assert step > 0
    assert slicelength >= 0
    if slicelength == 0:
        assert stop <= start
    elif step == 1:
        assert stop - start == slicelength

    return start, stop, step


def slicelength(start, stop, step):
    """
    This is the slicelength implementation from PySlice_AdjustIndices().

    a / b does integer division.  If either a or b is negative, the result
    depends on the compiler (rounding can go toward 0 or negative infinity).
    Therefore, we are careful that both a and b are always positive.
    """
    if step < 0:
        if stop < start:
            return (start - stop - 1) // (-step) + 1
    else:
        if start < stop:
            return (stop - start - 1) // step + 1
    return 0


class ListSliceTests(unittest.TestCase):

    def random_slices(self, max_len=100, repeat=10_000):
        for _ in range(repeat):
            n = randint(0, max_len)
            s = slice(randint(-n - 2, n + 2),
                      randint(-n - 2, n + 2),
                      randint(-5, 5) or 1)
            yield n, s, range(n)[s]

    def test_basic(self):
        for n, s, r in self.random_slices():
            self.assertEqual(range(*s.indices(n)), r)

    def test_indices(self):
        for n, s, r in self.random_slices():
            start, stop, step = s.indices(n)
            self.assertEqual(start, r.start)
            self.assertEqual(stop, r.stop)
            self.assertEqual(step, r.step)

            self.assertNotEqual(step, 0)
            if step > 0:
                self.assertTrue(0 <= start <= n)
                self.assertTrue(0 <= stop <= n)
            else:
                self.assertTrue(-1 <= start < n)
                self.assertTrue(-1 <= stop < n)
            self.assertEqual(range(start, stop, step), r)

    def test_list_get(self):
        for n, s, r in self.random_slices():
            a = list(range(n))
            b = a[s]
            self.assertEqual(len(b), len(r))
            self.assertEqual(b, list(r))

    def test_list_set(self):
        for n, s, r in self.random_slices(20):
            a = n * [None]
            b = list(a)
            a[s] = range(len(r))
            for i, j in enumerate(r):
                b[j] = i
            self.assertEqual(a, b)

    def test_list_del(self):
        for n, s, r in self.random_slices():
            a = list(range(n))
            b = list(a)
            del a[s]
            self.assertEqual(len(a), n - len(r))
            for i in sorted(r, reverse=True):
                del b[i]
            self.assertEqual(a, b)

    def test_adjust_step_positive(self):
        for n, s, r in self.random_slices():
            if s.step < 0:
                r = r[::-1]

            start, stop, step = adjust_step_positive(len(r), *s.indices(n))

            self.assertEqual(range(start, stop, step), r)
            self.assertTrue(step > 0)
            if r:
                self.assertTrue(0 <= start < n)
                self.assertTrue(0 < stop <= n)

    def test_slicelength(self):
        for n, s, r in self.random_slices():
            self.assertEqual(slicelength(r.start, r.stop, r.step), len(r))


class NXIR_Tests(unittest.TestCase):

    def test_nxir(self):
        for _ in range(1000):
            start = randrange(100)
            step = randrange(1, 20)
            r = range(start, sys.maxsize, step)

            def nxir(x): # set_range_opt() NXIR macro
                """
                return distance to next (or current) index in
                range(start, maxsize, step), such that:

                    (x + nxir(x)) in range(start, sys.maxsize, step)
                or
                    (x + nxir(x) - start) % step == 0
                """
                assert x - start >= 0
                return (step - (((x) - start) % step)) % step

            self.assertEqual(nxir(start), 0)
            for i in range(10):
                x = start + i * step
                self.assertEqual(nxir(x), 0)

            x = randrange(start, start + 100)
            self.assertTrue(x >= start)

            nx = nxir(x)
            self.assertTrue(nx in range(0, step))
            self.assertEqual((x + nx) % step, start % step)
            self.assertTrue((x + nx) in r)
            self.assertEqual((x + nx - start) % step, 0)


if __name__ == '__main__':
    unittest.main()
