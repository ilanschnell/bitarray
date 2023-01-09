"""
Implementation of a sparse bitarray

Internally we store a list of positions at a which a bit changes from
1 to 0 or vice versa.  Moreover, we start with bit 0, meaning that if the
first bit in the bitarray is 1 our list starts with posistion 0.
For example:

   bitarray('110011111000')

is represented as:

   changes:   [0, 2, 4, 9, 12]

The last element in the list is always the length of the bitarray, such that
an empty bitarray is represented as [0].
"""
from bisect import bisect

from bitarray import bitarray
from bitarray.util import intervals


class SparseBitarray:

    def __init__(self, a):
        lst = [] if a and a[0] == 0 else [0]
        lst.extend(t[2] for t in intervals(a))
        assert len(lst) > 0
        self.stops = lst

    def __len__(self):
        return self.stops[-1]

    def __getitem__(self, i):
        if not 0 <= i < len(self):
            raise IndexError
        return bisect(self.stops, i) % 2

    def __setitem__(self, i, value):
        if not 0 <= i < len(self):
            raise IndexError
        p = bisect(self.stops, i)
        if p % 2 == value:
            return
        self.stops[p:p] = [i, i + 1]
        self._reduce()

    def _reduce(self):
        n = len(self)           # length of bitarray
        lst = []                # new representation list
        i = 0
        while True:
            c = self.stops[i]   # current element (at index i)
            if c == n:          # element with bitarray length reached
                lst.append(n)
                break
            j = i + 1           # find next value (at index j)
            while self.stops[j] == c:
                j += 1
            if (j - i) % 2:     # only append index if repeated even times
                lst.append(c)
            i = j
        self.stops = lst

    def _intervals(self):
        v = 0
        start = 0
        for stop in self.stops:
            yield v, start, stop
            v = 1 - v
            start = stop

    def append(self, value):
        if value == len(self.stops) % 2:  # opposite value as last element
            self.stops.append(len(self) + 1)
        else:                             # same value as last element
            self.stops[-1] += 1

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

"""
a = bitarray('110011111000')
print(a)
s = SparseBitarray(a)
print(s.stops)
s.append(1)
print(s.stops)
s._reduce()
print(s.stops)
print(s.export())
print(a)
"""
# ---------------------------------------------------------------------------

from collections import Counter
from random import randint
import unittest

from bitarray.test_bitarray import Util


class TestsSparse(unittest.TestCase, Util):

    def check(self, s):
        stops = s.stops
        self.assertTrue(len(stops) > 0)
        self.assertEqual(stops, sorted(stops))
        cnt = Counter(stops)
        cnt[stops[-1]] = 1
        self.assertTrue(all(c == 1 for c in cnt.values()))

    def test_init_export(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            self.assertEqual(len(s), len(a))
            self.assertEqual(s.export(), a)
            self.check(s)

    def test_getitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                self.assertEqual(s[i], a[i])

    def test_setitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                v = randint(0, 1)
                s[i] = a[i] = v
                self.assertEqual(s.export(), a)
                self.check(s)

    def test_append(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                v = randint(0, 1)
                s.append(v)
                a.append(v)
                self.assertEqual(s.export(), a)
                self.check(s)

    def test_count(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            for v in 0, 1:
                self.assertEqual(s.count(v), a.count(v))

if __name__ == '__main__':
    unittest.main()
