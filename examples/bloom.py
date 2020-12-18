import sys
import hashlib
from math import ceil, exp, log

from bitarray import bitarray

if sys.version_info[0] == 2:
    int = long
    range = xrange
    log2 = lambda x: log(x) / log(2)
else:
    from math import log2


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
        self.k = int(ceil(-log2(p)))
        # size of array
        self.m = int(ceil(-n * log2(p) / log(2)))
        self.array = bitarray(self.m)
        self.array.setall(0)

    def calculate_p(self):
        """
        Calculate the actual false positive error rate `p` from the number
        of hashes `k` and the size if the bitarray `m`.  This is slightly
        different from the given `p`, because the integer value of `k`
        is being used.
        """
        return pow(1.0 - exp(-float(self.k) * self.n / self.m), self.k)

    def approx_items(self):
        """
        Return the approximate number of items in the Bloom filter.
        """
        count = self.array.count()
        if count == 0:
            return 0.0
        return -float(self.m) / self.k * log(1.0 - float(count) / self.m)

    def add(self, key):
        for i in self._hashes(key):
            self.array[i] = 1

    def __contains__(self, key):
        return all(self.array[i] for i in self._hashes(key))

    def _hashes(self, key):
        """
        generate k different hashes, each of which maps a key to one of
        the m array positions with a uniform random distribution
        """
        h = hashlib.new('md5')
        h.update(str(key).encode())
        x = int(h.hexdigest(), 16)
        for _unused in range(self.k):
            if x < 1024 * self.m:
                h.update(b'x')
                x = int(h.hexdigest(), 16)
            x, y = divmod(x, self.m)
            yield y


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
    print("approx_items(): %.2f" % b.approx_items())
    print("calculate_p(): %.3f%%" % (100.0 * b.calculate_p()))

    N = 100000
    false_pos = sum(i in b for i in range(n, n + N))
    print("experimental : %.3f%%\n" % (100.0 * false_pos / N))


if __name__ == '__main__':
    test_bloom(5000, 0.05)
    test_bloom(10000, 0.01)
    test_bloom(50000, 0.005)
    test_bloom(100000, 0.002)
