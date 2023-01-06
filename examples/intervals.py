from bitarray import bitarray


def intervals(__a):
    """intervals(bitarray, /) -> iterator

Compute all uninterrupted intervals of `0`s and `1`s, and return an
iterator over tuples (value, start, stop).
"""
    n = len(__a)
    if n == 0:
        return
    stop = 0
    value = __a[0]

    while stop < n:
        start = stop
        try:
            stop = __a.index(not value, stop)
        except ValueError:
            stop = n
        yield int(value), start, stop
        value = not value

# ---------------------------------------------------------------------------

import unittest

from bitarray.util import urandom
from bitarray.test_bitarray import Util


class TestsIntervals(unittest.TestCase, Util):

    def test_explicit(self):
        for s, lst in [
                ('', []),
                ('0', [(0, 0, 1)]),
                ('1', [(1, 0, 1)]),
                ('00111100 00000111 00',
                 [(0, 0, 2), (1, 2, 6), (0, 6, 13), (1, 13, 16), (0, 16, 18)]),
            ]:
            a = bitarray(s)
            self.assertEqual(list(intervals(a)), lst)

    def test_count(self):
        for s, res in [
                ('', 0),
                ('0', 1),
                ('1', 1),
                ('00', 1),
                ('01', 2),
                ('10', 2),
                ('11', 1),
                ('0011110000000', 3),
            ]:
            a = bitarray(s)
            self.assertEqual(res, len(list(intervals(a))))
            self.assertEqual(res, sum(1 for _ in intervals(a)))

    def test_random(self):
        for a in self.randombitarrays():
            b = urandom(len(a))
            cnt = [0, 0]
            v = a[0] if a else None
            for value, start, stop in intervals(a):
                self.assertFalse(isinstance(value, bool))
                self.assertEqual(value, v)
                v = not v
                self.assertTrue(0 <= start < stop <= len(a))
                cnt[value] += stop - start
                b[start:stop] = value
            self.assertEqual(a, b)
            for v in 0, 1:
                self.assertEqual(cnt[v], a.count(v))

    def test_runs(self):
        for a in self.randombitarrays():
            first = a[0] if a else None
            runs = []  # list runs of alternating bits
            for value, start, stop in intervals(a):
                runs.append(stop - start)

            b = bitarray()
            if first is not None:
                v = first
                for length in runs:
                    b.extend(length * bitarray([v]))
                    v = not v

            self.assertEqual(a, b)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
