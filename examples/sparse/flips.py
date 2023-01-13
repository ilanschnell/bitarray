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

from common import Common


class SparseBitarray(Common):

    def __init__(self, x = 0):
        if isinstance(x, int):
            self.flips = [x]  # bitarray with x zeros
        else:
            self.flips = [0]
            for v in x:
                self.append(int(v))

    def __len__(self):
        return self.flips[-1]

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
        a = bitarray(len(self))
        for v, start, stop in self._intervals():
            a[start:stop] = v
        return a

    def invert(self):
        self.flips.insert(0, 0)
        self._reduce()

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
