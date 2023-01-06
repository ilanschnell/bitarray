from bitarray import bitarray
from bitarray.util import intervals


class sparse:

    def __init__(self, a):
        self.first = a[0] if a else 0
        self.runs = [stop - start for _, start, stop in intervals(a)]

    def __len__(self):
        return sum(self.runs)

    def __getitem__(self, i):
        start = 0
        for p, cnt in enumerate(self.runs):
            stop = start + cnt
            if start <= i < stop:
                return (self.first + p) % 2
            start = stop
        raise IndexError

    def export(self):
        res = bitarray()
        v = self.first
        for length in self.runs:
            res.extend(length * bitarray([v]))
            v = not v
        return res

    def count(self, value=1):
        return sum(self.runs[self.first ^ value::2])

# ---------------------------------------------------------------------------

from random import randint
import unittest

from bitarray.test_bitarray import Util

class TestsSparse(unittest.TestCase, Util):

    def test_init_export(self):
        for a in self.randombitarrays():
            s = sparse(a)
            self.assertEqual(len(s), len(a))
            self.assertEqual(s.export(), a)

    def test_getitem(self):
        for a in self.randombitarrays(start=1):
            s = sparse(a)
            for _ in range(10):
                i = randint(0, len(s) - 1)
                self.assertEqual(s[i], a[i])

    def test_count(self):
        for a in self.randombitarrays():
            s = sparse(a)
            for v in 0, 1:
                self.assertEqual(s.count(v), a.count(v))

if __name__ == '__main__':
    unittest.main()
