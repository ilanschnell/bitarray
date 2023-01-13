import sys
from random import randint
import unittest

try:
    from itertools import pairwise  # type: ignore
except ImportError:
    from itertools import tee
    def pairwise(iterable):
        a, b = tee(iterable)
        next(b, None)
        return zip(a, b)

from bitarray.util import intervals
from bitarray.test_bitarray import Util

if len(sys.argv) != 2 or sys.argv[1] not in ('flips', 'ones', '-'):
    sys.exit("Argument 'flips' or 'ones' expected.")
MODE = sys.argv[1]
del sys.argv[1]


class TestsSparse(unittest.TestCase, Util):

    def check(self, s, a):
        if MODE == 'flips':
            self.assertTrue(len(s.flips) > 0 and s.flips[0] >= 0)
            for x, y in pairwise(s.flips):
                self.assertTrue(y > x)
            self.assertEqual(s.to_bitarray(), a)

        elif MODE == 'ones':
            for x, y in pairwise(s.ones):
                self.assertTrue(y > x)
            self.assertEqual(s.to_bitarray(), a)

        else:
            self.assertEqual(s, a)

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

    def test_insert(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            i = randint(-2, len(s) + 2)
            v = randint(0, 1)
            s.insert(i, v)
            a.insert(i, v)
            self.check(s, a)

    if MODE != 'ones':
        def test_invert(self):
            for a in self.randombitarrays():
                s = SparseBitarray(a)
                s.invert()
                a.invert()
                self.check(s, a)

    if MODE == 'flips':
        def test_flips(self):
            for a in self.randombitarrays():
                lst = [] if a and a[0] == 0 else [0]
                lst.extend(t[2] for t in intervals(a))
                s = SparseBitarray(a)
                self.assertEqual(s.flips, lst)

    def test_reverse(self):
        for a in self.randombitarrays():
            s = SparseBitarray(a)
            s.reverse()
            a.reverse()
            self.check(s, a)

    if MODE == 'flips':
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
                    ([0, 0, 0, 1, 1, 2, 2, 2, 3, 4, 4, 4, 4, 5],
                     [0, 2, 3, 5]),
                ]:
                s = SparseBitarray()
                s.flips = a
                s._reduce()
                self.assertEqual(s.flips, b)


if __name__ == '__main__':
    if MODE == '-':
        from bitarray import bitarray
        SparseBitarray = bitarray
    else:
        SparseBitarray = __import__(MODE).SparseBitarray  # type: ignore
    unittest.main()
