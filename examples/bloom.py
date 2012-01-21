import hashlib
from math import exp, log

from bitarray import bitarray


class BloomFilter(object):

    def __init__(self, m, k):
        self.m = m
        self.k = k
        self.array = bitarray(m)
        self.array.setall(0)

    def add(self, key):
        for j in xrange(self.k):
            self.array[self._hash(key, j)] = 1

    def contains(self, key):
        return all(self.array[self._hash(key, j)] for j in xrange(self.k))

    def _hash(self, key, i):
        return long(hashlib.sha1('%s:%s' % (key, i)).hexdigest(), 16) % self.m



def test_bloom(m, k, n):
    b = BloomFilter(m, k)
    for i in xrange(n):
        b.add(i)
        assert b.contains(i)

    p = (1.0 - exp(-k * (n + 0.5) / (m - 1))) ** k
    print 100.0 * p, '%'

    N = 10000
    false_pos = sum(b.contains(i) for i in xrange(n, n + N))
    print 100.0 * false_pos / N, '%'


if __name__ == '__main__':
    test_bloom(10000, 9, 800)
