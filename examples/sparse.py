"""
Implementation of a sparse bitarray

Internally we store a list of positions at a which a bit changes from
1 to 0 or vice versa.  Moreover, we start with bit 0, meaning that if the
first bit in the bitarray is 1 our list starts with posistion 0.
For example:

   bitarray('110011111000')

is represented as:

   flips:   [0, 2, 4, 9, 12]

The last element in the list is always the length of the bitarray, such that
an empty bitarray is represented as [0].
"""
from bisect import bisect, bisect_left

from bitarray import bitarray


class SparseBitarray:

    def __init__(self, x = 0):
        if isinstance(x, int):
            self.flips = [x]  # bitarray with x zeros
        else:
            self.flips = [0]
            for v in x:
                self.append(int(v))

    def __len__(self):
        return self.flips[-1]

    def __delitem__(self, i):
        if not 0 <= i < len(self):
            raise IndexError
        p = bisect(self.flips, i)
        for j in range(p, len(self.flips)):
            self.flips[j] -= 1
        self._reduce()

    def __getitem__(self, i):
        if not 0 <= i < len(self):
            raise IndexError
        return bisect(self.flips, i) % 2

    def __setitem__(self, i, value):
        if not 0 <= i < len(self):
            raise IndexError
        p = bisect(self.flips, i)
        if p % 2 == value:
            return
        self.flips[p:p] = [i, i + 1]
        self._reduce()

    def _reduce(self):
        n = len(self)           # length of bitarray
        lst = []                # new representation list
        i = 0
        while True:
            c = self.flips[i]   # current element (at index i)
            if c == n:          # element with bitarray length reached
                lst.append(n)
                break
            j = i + 1           # find next value (at index j)
            while self.flips[j] == c:
                j += 1
            if (j - i) % 2:     # only append index if repeated even times
                lst.append(c)
            i = j
        self.flips = lst

    def _intervals(self):
        v = 0
        start = 0
        for stop in self.flips:
            yield v, start, stop
            v = 1 - v
            start = stop

    def append(self, value):
        if value == len(self.flips) % 2:  # opposite value as last element
            self.flips.append(len(self) + 1)
        else:                             # same value as last element
            self.flips[-1] += 1

    def to_bitarray(self):
        res = bitarray(len(self))
        for v, start, stop in self._intervals():
            res[start:stop] = v
        return res

    def invert(self):
        self.flips.insert(0, 0)
        self._reduce()

    def _adjust_index(self, i):
        n = len(self)
        if i < 0:
            i += n
            if i < 0:
                i = 0
        elif i > n:
            i = n
        return i

    def insert(self, i, value):
        i = self._adjust_index(i)
        p = bisect_left(self.flips, i)
        for j in range(p, len(self.flips)):
            self.flips[j] += 1
        self[i] = value

    def count(self, value=1):
        cnt = 0
        for v, start, stop in self._intervals():
            if v == value:
                cnt += stop - start
        return cnt

    def reverse(self):
        n = len(self)
        lst = [0] if len(self.flips) % 2 else []
        lst.extend(n - p for p in reversed(self.flips))
        lst.append(n)
        self.flips = lst
        self._reduce()

"""
s = SparseBitarray('10')
a = bitarray(s)
print(a)
print(s.flips)
del s[1]
del a[1]
print(s.flips)
print(s.to_bitarray())
print(a)
"""
# ---------------------------------------------------------------------------

import sys
from random import randint
import unittest

if sys.version_info >= (3, 10):
    from itertools import pairwise
else:
    from itertools import tee
    def pairwise(iterable):
        a, b = tee(iterable)
        next(b, None)
        return zip(a, b)

from bitarray.util import intervals
from bitarray.test_bitarray import Util


class TestsSparse(unittest.TestCase, Util):

    def check(self, s, a):
        flips = s.flips
        self.assertTrue(len(flips) > 0 and flips[0] >= 0)
        for x, y in pairwise(flips):
            self.assertTrue(y > x)

        self.assertEqual(s.to_bitarray(), a)

    def test_flips(self):
        for a in self.randombitarrays():
            lst = [] if a and a[0] == 0 else [0]
            lst.extend(t[2] for t in intervals(a))
            s = SparseBitarray(a)
            self.assertEqual(s.flips, lst)

    def test_len(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            self.assertEqual(len(s), len(a))
            self.check(s, a)

    def test_getitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for i in range(len(a)):
                self.assertEqual(s[i], a[i])

    def test_delitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            i = randint(0, len(s) - 1)
            del s[i]
            del a[i]
            self.check(s, a)

    def test_setitem(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                v = randint(0, 1)
                s[i] = a[i] = v
                self.check(s, a)

    def test_append(self):
        for a in self.randombitarrays():
            s = SparseBitarray()
            for v in a:
                s.append(v)
            self.check(s, a)

    def test_count(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            for v in 0, 1:
                self.assertEqual(s.count(v), a.count(v))

    def test_invert(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            s.invert()
            a.invert()
            self.check(s, a)

    def test_insert(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            i = randint(-2, len(s) + 2)
            v = randint(0, 1)
            s.insert(i, v)
            a.insert(i, v)
            self.check(s, a)

    def test_reverse(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            s.reverse()
            a.reverse()
            self.check(s, a)

if __name__ == '__main__':
    unittest.main()
