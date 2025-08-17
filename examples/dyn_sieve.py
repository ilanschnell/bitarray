import math
import itertools

from bitarray import bitarray
from bitarray.util import ones, count_n


class Sieve:
    """
    Prime numbers, implemented as a dynamically growing sieve of
    Eratosthenes.  Similar to prime number sieve in SymPy, but implemented
    using a bitarray.
    """
    a = ones(105)
    a[1::3] = 0
    a[2::5] = 0
    a[3::7] = 0

    def __init__(self):
        self.data = bitarray()

    def extend(self, i):
        "grow to accomodate i, ie. self.data[i//2] will not raise IndexError"
        if i < 0:
            raise ValueError("positive integer expected")

        if i == 0:  # reset
            self.data = bitarray()
            return

        n = i // 2 + 1  # n is minimal length of self.data
        if n <= len(self.data):
            return

        fresh_data = not self.data
        if fresh_data:
            self.data = self.a.copy()
            self.data[:8] = bitarray("01110110")

        a = self.data
        n1 = len(a)
        m = (n - n1 + 105 - 1) // 105
        assert fresh_data or m > 0
        a += m * self.a
        if fresh_data:
            n1 = 60
        for i in a.search(1, 5, int(math.sqrt(len(a) // 2) + 1.0)):
            j = 2 * i + 1
            j2 = (j * j) // 2
            k = (j2 - n1) % j + n1 if j2 < n1 else j2
            assert k >= n1
            a[k :: j] = 0

    def extend_to_no(self, n):
        while self.data.count() + 1 < n:
            self.extend(3 * len(self.data) + 1)

    def __contains__(self, i):
        if i < 0:
            raise ValueError("positive integer expected")
        if i % 2 == 0:
            return i == 2
        self.extend(i)
        return self.data[i // 2]

    def __iter__(self):
        yield 2
        for i in itertools.count(start=3, step=2):
            self.extend(i)
            if self.data[i // 2]:
                yield i

    def __getitem__(self, n):
        "return n-th prime"
        if n < 1:
            # offset is one, so forbid explicit access to sieve[0]
            raise IndexError("Sieve indices start at 1")
        if n == 1:
            return 2
        self.extend_to_no(n)
        i = count_n(self.data, n - 1) - 1
        assert self.data[i]
        return 2 * i + 1

# ---------------------------------------------------------------------------

import unittest
from random import randrange

from bitarray.util import gen_primes


N = 1_000_000
PRIMES = gen_primes(N)
ODD_PRIMES = PRIMES[1::2]


class SieveTests(unittest.TestCase):

    def check_data(self, s, i):
        if i == 0:
            self.assertEqual(len(s.data), 0)
            return
        n = i // 2 + 1
        if n <= len(s.data):
            n = len(s.data)
        n = 105 * ((n + 105 - 1) // 105)
        self.assertEqual(len(s.data), n)
        self.assertEqual(s.data, ODD_PRIMES[:n])

    def test_random(self):
        s = Sieve()
        for _ in range(1000):
            i = randrange(1000) if randrange(10) else 0
            s.extend(i)
            self.check_data(s, i)
            #print(n, len(s.data))

    def test_contains(self):
        s = Sieve()
        for i, v in enumerate(PRIMES[:1000]):
            self.assertEqual(i in s, v)
        for _ in range(1000):
            i = randrange(1_000_000)
            self.assertEqual(i in s, PRIMES[i])

    def test_iter(self):
        s = Sieve()
        a = []
        for i in s:
            if len(a) >= 168:
                break
            a.append(i)
        self.assertEqual(a[-1], 997)
        self.assertEqual(a, list(PRIMES.search(1, 0, 1000)))

    def test_getitem(self):
        s = Sieve()
        self.assertEqual(s[1], 2)
        self.assertEqual(s[2], 3)
        self.assertEqual(s[3], 5)
        self.assertEqual(s[10], 29)
        self.assertEqual(s[1_000_000], 15_485_863)


if __name__ == '__main__':
    unittest.main()
