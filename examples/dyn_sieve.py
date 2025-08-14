from math import sqrt

from bitarray import bitarray
from bitarray.util import ones, count_n


class Sieve:
    """
    Prime numbers, implemented as a dynamically growing sieve of
    Eratosthenes.  Similar to prime number sieve in SymPy, but implemented
    using a bitarray.
    """
    a = ones(210)
    for i in 2, 3, 5, 7:
        a[::i] = 0

    def __init__(self):
        self.data = bitarray(0)

    def __len__(self):
        return len(self.data)

    def extend(self, n):
        n = int(n)
        if n < 0:
            raise ValueError("bitarray length must be >= 0")

        if n == 0:  # reset
            self.data = bitarray(0)
            return

        if n <= len(self):
            return

        fresh_data = not self.data
        if fresh_data:
            self.data = self.a.copy()
            self.data[:8] = bitarray("00110101")

        a = self.data
        n1 = len(a)
        m = (n - n1 + 210 - 1) // 210
        self.data += m * self.a
        if fresh_data:
            n1 = 121
        for i in a.search(1, 11, int(sqrt(len(a)) + 1.0)):
            i2 = i * i
            k = (i2 - n1) % i + n1 if i2 < n1 else i2
            assert k >= n1
            a[k :: i] = 0

    def extend_to_no(self, n):
        self.extend(1)
        while self.data.count() < n:
            self.extend(int(len(self) * 1.5))

    def __contains__(self, p):
        if p < 0:
            raise ValueError("positive integer expected")
        self.extend(p + 1)
        return self.data[p]

    def __iter__(self):
        i = 1
        while True:
            self.extend(i + 1)
            if self.data[i]:
                yield i
            i += 1

    def __getitem__(self, n):
        "return n-th prime"
        if isinstance(n, slice):
            self.extend_to_no(n.stop)
            start = n.start if n.start is not None else 0
            if start < 1:
                # sieve[:5] would be empty (starting at -1), let's
                # just be explicit and raise
                raise IndexError("Sieve indices start at 1.")
            a = count_n(self.data, start) - 1
            b = count_n(self.data, n.stop) - 1
            lst = list(self.primerange(a, b))
            return lst[::n.step]

        if n < 1:
            # offset is one, so forbid explicit access to sieve[0]
            raise IndexError("Sieve indices start at 1")
        self.extend_to_no(n)
        return count_n(self.data, n) - 1

    def primerange(self, a, b):
        self.extend(b + 1)
        yield from self.data.search(1, a, b)

# ---------------------------------------------------------------------------

import unittest
from itertools import islice
from random import randrange

from bitarray.util import gen_primes


N = 12_000_000
PRIMES = gen_primes(N)


class SieveTests(unittest.TestCase):

    def check_data(self, s, n):
        if n == 0:
            self.assertEqual(len(s), 0)
            return
        if n <= len(s):
            n = len(s)
        n = 210 * ((n + 210 - 1) // 210)
        self.assertEqual(len(s), n)
        self.assertEqual(s.data, PRIMES[:n])

    def test_random(self):
        s = Sieve()
        for _ in range(1000):
            n = randrange(1000) if randrange(10) else 0
            s.extend(n)
            self.check_data(s, n)
            #print(n, len(s))

    def test_iter(self):
        s = Sieve()
        it = islice(s, 10)
        self.assertEqual(list(it), [2, 3, 5, 7, 11, 13, 17, 19, 23, 29])

    def test_is_prime(self):
        s = Sieve()
        for i, v in enumerate(PRIMES[:1000]):
            self.assertEqual(i in s, v)
        for _ in range(1000):
            i = randrange(1_000_000)
            self.assertEqual(i in s, PRIMES[i])

    def test_ith_prime(self):
        s = Sieve()
        self.assertEqual(s[10], 29)
        self.assertEqual(s[1_000_000], 15_485_863)
        self.assertEqual(s[10:12], [29, 31])
        self.assertEqual(s[1:12], [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31])
        self.assertEqual(s[2:7], [3, 5, 7, 11, 13])
        self.assertEqual(s[2:7:2], [3, 7, 13])

    def test_primerange(self):
        s = Sieve()
        self.assertEqual(list(s.primerange(7, 19)), [7, 11, 13, 17])
        self.assertEqual(list(s.primerange(80, 100)), [83, 89, 97])

    def test_count(self):
        s = Sieve()
        for n, res in [
                (    10,    4),
                (   100,   25),
                ( 1_000,  168),
                (10_000, 1229),
        ]:
            s.extend(n)
            self.assertEqual(s.data.count(1, 0, n), res)


if __name__ == '__main__':
    unittest.main()
