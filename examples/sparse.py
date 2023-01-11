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

    def __repr__(self):
        return "SparseBitarray('%s')" % (''.join(str(v) for v in self))

    def __len__(self):
        return self.flips[-1]

    def _get_start_stop(self, key):
        if key.step not in (1, None):
            raise ValueError("only step = 1 allowed, got %r" % key)
        start = key.start
        if start is None:
            start = 0
        stop = key.stop
        if stop is None:
            stop = len(self)
        return start, stop

    def __getitem__(self, key):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return SparseBitarray()

            i = bisect(self.flips, start)
            j = bisect_left(self.flips, stop)

            res = SparseBitarray()
            res.flips = [0] if i % 2 else []
            for k in range(i, j):
                res.flips.append(self.flips[k] - start)
            res.flips.append(stop - start)
            return res

        elif isinstance(key, int):
            if not 0 <= key < len(self):
                raise IndexError
            return bisect(self.flips, key) % 2

        else:
            raise TypeError

    def __setitem__(self, key, value):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return

            i = bisect(self.flips, start)
            j = bisect_left(self.flips, stop)

            self.flips[i:j] = (
                ([] if i % 2 == value else [start]) +
                ([] if j % 2 == value else [stop])
            )

        elif isinstance(key, int):
            if not 0 <= key < len(self):
                raise IndexError
            p = bisect(self.flips, key)
            if p % 2 == value:
                return
            self.flips[p:p] = [key, key + 1]

        else:
            raise TypeError

        self._reduce()

    def __delitem__(self, key):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return

            i = bisect(self.flips, start)
            j = bisect_left(self.flips, stop)

            for k in range(j, len(self.flips)):
                self.flips[k] -= stop - start
            self.flips[i:j] = [start] if (j - i) % 2 else []

        elif isinstance(key, int):
            if not 0 <= key < len(self):
                raise IndexError
            p = bisect(self.flips, key)
            for j in range(p, len(self.flips)):
                self.flips[j] -= 1

        else:
            raise TypeError

        self._reduce()

    def _reduce(self):
        n = self.flips[-1]      # length of bitarray
        lst = []                # new representation list
        i = 0
        while True:
            c = self.flips[i]   # current element (at index i)
            if c == n:          # element with bitarray length reached
                break
            j = i + 1           # find next value (at index j)
            while self.flips[j] == c:
                j += 1
            if (j - i) % 2:     # only append index if repeated odd times
                lst.append(c)
            i = j
        lst.append(n)
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

    def extend(self, other):
        n = len(self)
        m = len(other.flips)
        if len(self.flips) % 2:
            self.flips.append(n)
        for i in range(m):
            self.flips.append(other.flips[i] + n)
        self._reduce()

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

# ---------------------------------------------------------------------------

from random import randint
import unittest

try:
    from itertools import pairwise
except ImportError:
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

    def test_getitem_index(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for i in range(len(a)):
                self.assertEqual(s[i], a[i])

    def test_getitem_slice(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            i = randint(0, len(s))
            j = randint(0, len(s))
            t = s[i:j]
            b = a[i:j]
            self.check(t, b)
            self.check(s, a)

    def test_setitem_index(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                v = randint(0, 1)
                s[i] = a[i] = v
                self.check(s, a)

    def test_setitem_slice(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            for _ in range(10):
                i = randint(0, len(s))
                j = randint(0, len(s))
                v = randint(0, 1)
                s[i:j] = a[i:j] = v
                self.check(s, a)

    def test_delitem_index(self):
        for a in self.randombitarrays(start=1):
            s = SparseBitarray(a)
            i = randint(0, len(s) - 1)
            del s[i]
            del a[i]
            self.check(s, a)

    def test_delitem_slice(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            i = randint(0, len(s))
            j = randint(0, len(s))
            del s[i:j]
            del a[i:j]
            self.check(s, a)

    def test_append(self):
        for a in self.randombitarrays():
            s = SparseBitarray()
            for v in a:
                s.append(v)
            self.check(s, a)

    def test_extent(self):
        for aa in self.randombitarrays():
            for b in self.randombitarrays():
                a = aa.copy()
                s = SparseBitarray(a)
                t = SparseBitarray(b)
                s.extend(t)
                a.extend(b)
                self.check(s, a)
                self.check(t, b)

            s = SparseBitarray(aa)
            s.extend(s)
            self.check(s, 2 * aa)

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

    def test_reduce(self):
        for a, b in [
                ([0],                 [0]),
                ([0, 0],              [0]),
                ([3, 7],              [3, 7]),
                ([3, 7, 7],           [3, 7]),
                ([3, 3, 7, 7, 7],     [7]),
                ([3, 3, 3, 7, 7],     [3, 7]),
                ([0, 0, 2, 2],        [2]),
                ([0, 2, 2, 2, 2, 3],  [0, 3]),
                ([0, 0, 0, 1, 1, 2, 2, 2, 3, 4, 4, 4, 4, 5],  [0, 2, 3, 5]),
            ]:
            s = SparseBitarray()
            s.flips = a
            s._reduce()
            self.assertEqual(s.flips, b)

if __name__ == '__main__':
    unittest.main()
