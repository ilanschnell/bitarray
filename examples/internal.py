"""
This file contains some "hacks" which are used in the C implementation of
bitarray.
"""
import unittest


class InternalTests(unittest.TestCase):

    def test_range_check(self):
        # used in various places in C code
        for i in range(0, 11):
            for k in range(-10, 2000):
                res1 = k not in range(0, 1 << i)
                # simply shift i to right and see if anything remains
                res2 = bool(k >> i)
                self.assertEqual(res1, res2)

    def test_range_check_2(self):
        # this is used in _util.c in set_count()
        for i in range(0, 11):
            for k in range(-10, 2000):
                res1 = k not in range(0, (1 << i) + 1)
                # same as above but combined with k substracted by 1
                res2 = bool(k >> i) and bool((k - 1) >> i)
                self.assertEqual(res1, res2)


if __name__ == '__main__':
    unittest.main()
