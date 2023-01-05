from bitarray import bitarray


def ranges(a, value=1):
    """ranges(a, value=1) -> iterator

Iterate over all tuples (start, stop) which contain contiguous `1`s (or
specified by `value`) in `range(start, stop)`) in bitarray `a`.
"""
    stop = 0

    while True:
        try:
            start = a.index(value, stop)
        except ValueError:
            return

        try:
            stop = a.index(not value, start)
        except ValueError:
            stop = len(a)

        yield start, stop


def intervals(a):
    """intervals(a) -> int

Return the number of uninterrupted intervals of `0`s and `1`s in bitarray `a`.
"""
    if not a:
        return 0
    cnt = 1
    i = 0
    x = a[0]

    while True:
        try:
            x = not x
            i = a.index(x, i)
        except ValueError:
            return cnt
        cnt += 1

# ---------------------------------------------------------------------------

import unittest

from bitarray.util import urandom
from bitarray.test_bitarray import Util


class RangesTests(unittest.TestCase, Util):

    def test_explicit(self):
        for s, lst0, lst1 in [
                ('', [], []),
                ('0', [(0, 1)], []),
                ('1', [], [(0, 1)]),
                ('00111100 00000111 00',
                 [(0, 2), (6, 13), (16, 18)],
                 [(2, 6), (13, 16)]),
            ]:
            a = bitarray(s)
            self.assertEqual(list(ranges(a)), lst1)
            self.assertEqual(list(ranges(a, 0)), lst0)
            self.assertEqual(list(ranges(a, 1)), lst1)

    def test_random(self):
        for value in 0, 1:
            for a in self.randombitarrays():
                b = bitarray(len(a))
                b.setall(not value)
                cnt = 0
                for start, stop in ranges(a, value):
                    self.assertTrue(0 <= start < stop <= len(a))
                    b[start:stop] = value
                    cnt += stop - start
                self.assertEqual(a, b)
                self.assertEqual(a.count(value), cnt)

    def test_random2(self):
        for a in self.randombitarrays():
            b = urandom(len(a))
            for v in range(2):
                for start, stop in ranges(a, v):
                    b[start:stop] = v
            self.assertEqual(a, b)


class IntervalsTests(unittest.TestCase, Util):

    def chk_intervals(self, a):
        s = a.to01()
        while '00' in s:
            s = s.replace('00', '0')
        while '11' in s:
            s = s.replace('11', '1')
        self.assertEqual(intervals(a), len(s))

        self.assertEqual(intervals(a),
                         len(list(ranges(a, 0)) + list(ranges(a, 1))))

    def test_explicit(self):
        for s, res in [('', 0),
                     ('0', 1),
                     ('1', 1),
                     ('00', 1),
                     ('01', 2),
                     ('10', 2),
                     ('11', 1),
                     ('0011110000000', 3),
            ]:
            a = bitarray(s)
            self.assertEqual(intervals(a), res)
            self.chk_intervals(a)

    def test_random(self):
        for a in self.randombitarrays():
            self.chk_intervals(a)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
