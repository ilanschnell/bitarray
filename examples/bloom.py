import hashlib
from math import ceil, exp, log, log2

from bitarray.util import zeros


class BloomFilter(object):
    """
    Implementation of a Bloom filter.  An instance is initialized by
    it's capacity `n` and error rate `p`.  The capacity tells how many
    elements can be stored while maintaining no more than `p` false
    positives.
    """
    def __init__(self, n, p=0.01):
        assert 0 < p < 1
        self.n = n
        # number of hash functions
        self.k = ceil(-log2(p))
        # size of array
        self.m = ceil(-n * log2(p) / log(2))
        self.array = zeros(self.m)

    def calculate_p(self):
        """
        Calculate the actual false positive error rate `p` from the number
        of hashes `k` and the size if the bitarray `m`.  This is slightly
        different from the given `p`, because the integer value of `k`
        is being used.
        """
        return pow(1 - exp(-self.k * self.n / self.m), self.k)

    def approx_items(self):
        """
        Return the approximate number of items in the Bloom filter.
        """
        x = self.array.count()
        if x == 0:
            return 0.0
        return -self.m / self.k * log(1 - x / self.m)

    def add(self, key):
        self.array[list(self._hashes(key))] = 1

    def __contains__(self, key):
        return all(self.array[i] for i in self._hashes(key))

    def _hashes(self, key):
        """
        generate k different hashes, each of which maps a key to one of
        the m array positions with a uniform random distribution
        """
        n = 1 << self.m.bit_length()
        h = hashlib.new('sha1')
        h.update(str(key).encode())
        x = 0
        i = 0
        while i < self.k:
            if x < n:
                h.update(b'X')
                x = int.from_bytes(h.digest(), 'little')
            x, y = divmod(x, n)
            if y < self.m:
                yield y
                i += 1


def test_bloom(n, p):
    print("Testing Bloom filter:")
    print("capacity     n = %d" % n)
    print("given        p = %.3f%%" % (100.0 * p))
    b = BloomFilter(n, p)
    print("hashes       k = %d = ceil(%.3f)" % (b.k, -log2(p)))
    print("array size   m = %d" % b.m)
    for i in range(n):
        b.add(i)
        assert i in b
    print("population: %.2f%%" % (100.0 * b.array.count() / b.m))
    print("approx_items(): %.2f" % b.approx_items())
    print("calculate_p(): %.3f%%" % (100.0 * b.calculate_p()))

    N = 100_000
    false_pos = sum(i in b for i in range(n, n + N))
    print("experimental : %.3f%%\n" % (100.0 * false_pos / N))


if __name__ == '__main__':
    test_bloom(  5_000, 0.05)
    test_bloom( 10_000, 0.01)
    test_bloom( 50_000, 0.005)
    test_bloom(100_000, 0.002)
