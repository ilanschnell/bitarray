from bitarray import bitarray


def ranges(a):
    """ranges(a) -> iterator

Iterate over all tuples (start, stop) which contain contiguous `1`s (in
`range(start, stop)`) in bitarray `a`.
"""
    stop = 0

    while True:
        try:
            start = a.index(1, stop)
        except ValueError:
            return

        try:
            stop = a.index(0, start)
        except ValueError:
            stop = len(a)

        yield start, stop


def intervals(a):
    """intervals(a) -> int

Return the number of uninterrupted intervals of `0`s and `1`s in bitarray `a`.
"""
    if not a:
        return 0
    cnt = 0
    i = 0
    x = a[0]

    while True:
        cnt += 1
        try:
            x = not x
            i = a.index(x, i)
        except ValueError:
            return cnt

# ---------------------------------------------------------------------------

import unittest

from bitarray.util import zeros
from bitarray.test_bitarray import Util


class RangesTests(unittest.TestCase, Util):

    def test_explicit(self):
        for s, lst in [('', []),
                       ('0', []),
                       ('1', [(0, 1)]),
                       ('00111100 00000111 00', [(2, 6), (13, 16)]),
            ]:
            self.assertEqual(list(ranges(bitarray(s))), lst)

    def test_random(self):
        for a in self.randombitarrays():
            b = zeros(len(a))
            cnt = 0
            for start, stop in ranges(a):
                self.assertTrue(0 <= start < stop <= len(a))
                b[start:stop] = 1
                cnt += stop - start
            self.assertEqual(a, b)
            self.assertEqual(a.count(), cnt)

class IntervalsTests(unittest.TestCase, Util):

    def chk_intervals(self, a):
        s = a.to01()
        while '00' in s:
            s = s.replace('00', '0')
        while '11' in s:
            s = s.replace('11', '1')
        self.assertEqual(intervals(a), len(s))

        if a:
            c = 2 * len(list(ranges(a))) - 1
            c += not a[0]
            c += not a[-1]
        else:
            c = 0
        self.assertEqual(intervals(a), c)

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
