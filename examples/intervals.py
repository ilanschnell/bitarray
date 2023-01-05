from bitarray import bitarray


def intervals(__a):
    """intervals(bitarray, /) -> iterator

Iterate over all tuples (value, start, stop) which contain uninterrupted
intervals of `value` in the bitarray.
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
        yield value, start, stop
        value = int(not value)

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
            cnt = {0: 0, 1: 0}
            values = bitarray()
            for value, start, stop in intervals(a):
                self.assertTrue(0 <= start < stop <= len(a))
                cnt[value] += stop - start
                values.append(value)
                b[start:stop] = value
            self.assertEqual(a, b)
            for v in 0, 1:
                self.assertEqual(cnt[v], a.count(v))
            self.assertFalse(bitarray('00') in values)
            self.assertFalse(bitarray('11') in values)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
