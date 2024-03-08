"""
Implementation of a sparse bitarray

For example:

   bitarray('110011111000')

is represented as:

   length:  11
   ones:    [0, 1, 4, 5, 6, 7, 8]
"""
from bisect import bisect_left

from bitarray import bitarray

from common import Common


class SparseBitarray(Common):

    def __init__(self, x = 0):
        if isinstance(x, int):
            self.n = x
            self.ones = []
        else:
            self.n = 0
            self.ones = []
            for v in x:
                self.append(int(v))

    def __len__(self):
        return self.n

    def __getitem__(self, key):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return SparseBitarray()

            i = bisect_left(self.ones, start)
            j = bisect_left(self.ones, stop)
            res = SparseBitarray(stop - start)
            for k in range(i, j):
                res.ones.append(self.ones[k] - start)
            return res

        elif isinstance(key, int):
            if not 0 <= key < self.n:
                raise IndexError
            i = bisect_left(self.ones, key)
            return int(i != len(self.ones) and self.ones[i] == key)

        else:
            raise TypeError

    def __setitem__(self, key, value):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return

            i = bisect_left(self.ones, start)
            j = bisect_left(self.ones, stop)
            del self.ones[i:j]
            if value == 0:
                return
            self.ones.extend(range(start, stop))
            self.ones.sort()

        elif isinstance(key, int):
            if not 0 <= key < self.n:
                raise IndexError
            i = bisect_left(self.ones, key)
            if i != len(self.ones) and self.ones[i] == key:  # key present
                if value == 0:
                    del self.ones[i]
            else:  # key not present
                if value == 1:
                    self.ones.insert(i, key)

        else:
            raise TypeError

    def __delitem__(self, key):
        if isinstance(key, slice):
            start, stop = self._get_start_stop(key)
            if stop <= start:
                return

            i = bisect_left(self.ones, start)
            j = bisect_left(self.ones, stop)
            del self.ones[i:j]
            size = stop - start
            for k in range(i, len(self.ones)):
                self.ones[k] -= size
            self.n -= size

        elif isinstance(key, int):
            if not 0 <= key < len(self):
                raise IndexError
            i = bisect_left(self.ones, key)
            if i != len(self.ones) and self.ones[i] == key:
                del self.ones[i]
            for k in range(i, len(self.ones)):
                self.ones[k] -= 1
            self.n -= 1

        else:
            raise TypeError

    def append(self, value):
        if value:
            self.ones.append(self.n)
        self.n += 1

    def find(self, value):
        ones = self.ones
        if value:
            return ones[0] if ones else -1
        else:
            m = len(ones)
            if m == self.n:
                return -1
            for i in range(m):
                if ones[i] != i:
                    return i
            return m

    def extend(self, other):
        self.ones.extend(other.ones[i] + self.n for i in
                         range(len(other.ones)))
        self.n += other.n

    def to_bitarray(self):
        a = bitarray(self.n)
        a.setall(0)
        a[self.ones] = 1
        return a

    def insert(self, k, value):
        k = self._adjust_index(k)
        i = bisect_left(self.ones, k)
        for j in range(i, len(self.ones)):
            self.ones[j] += 1
        self.n += 1
        self[k] = value

    def invert(self):
        self.ones = sorted(set(range(self.n)) - set(self.ones))

    def count(self, value=1):
        if value:
            return len(self.ones)
        else:
            return self.n - len(self.ones)

    def reverse(self):
        lst = [self.n - i - 1 for i in self.ones]
        lst.reverse()
        self.ones = lst
