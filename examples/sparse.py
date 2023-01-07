"""
Implementation of a sparse bitarray

Internally, we store the value of the first bit, and a list of positions at
which a bit changes from 1 to 0 or vice versa.  For example:

   bitarray('110011111000')

is represented as:

   first bit: 1
   changes:   [2, 4, 9, 12]
"""
from bisect import bisect

from bitarray import bitarray
from bitarray.util import intervals


class SparseBitarray:

    def __init__(self, a):
        self.first = a[0] if a else None
        self.stops = [t[2] for t in intervals(a)]

    def __len__(self):
        return self.stops[-1] if self.stops else 0

    def __getitem__(self, i):
        if not 0 <= i < len(self):
            raise IndexError
        p = bisect(self.stops, i)
        return (self.first + p) % 2

    def _intervals(self):
        v = self.first
        start = 0
        for stop in self.stops:
            yield int(v), start, stop
            v = not v
            start = stop

    def export(self):
        res = bitarray(len(self))
        for v, start, stop in self._intervals():
            res[start:stop] = v
        return res

    def count(self, value=1):
        cnt = 0
        for v, start, stop in self._intervals():
            if v == value:
                cnt += stop - start
        return cnt

# ---------------------------------------------------------------------------

from random import randint
import unittest

from bitarray.test_bitarray import Util


class TestsSparse(unittest.TestCase, Util):

    def test_init_export(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            self.assertEqual(len(s), len(a))
            self.assertEqual(s.export(), a)

    def test_getitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                self.assertEqual(s[i], a[i])

    def test_count(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            for v in 0, 1:
                self.assertEqual(s.count(v), a.count(v))

if __name__ == '__main__':
    unittest.main()
