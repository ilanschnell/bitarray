"""
Demonstrates the implementation of a Bloom filter, see:
http://en.wikipedia.org/wiki/Bloom_filter
"""
from __future__ import print_function
import sys

if sys.version_info > (3,):
    long = int
    xrange = range

import hashlib
from math import exp

from bitarray import bitarray


class BloomFilter(object):

    def __init__(self, m, k):
        self.m = m
        self.k = k
        self.array = bitarray(m)
        self.array.setall(0)

    def add(self, key):
        for i in self._hashes(key):
            self.array[i] = 1

    def contains(self, key):
        return all(self.array[i] for i in self._hashes(key))

    def _hashes(self, key):
        """
        generate k different hashes, each of which maps a key to one of
        the m array positions with a uniform random distribution
        """
        h = hashlib.new('md5')
        h.update(str(key).encode())
        x = long(h.hexdigest(), 16)
        for _unused in xrange(self.k):
            if x < self.m:
                h.update('.')
                x = long(h.hexdigest(), 16)
            x, y = divmod(x, self.m)
            yield y


def test_bloom(m, k, n):
    b = BloomFilter(m, k)
    for i in xrange(n):
        b.add(i)
        assert b.contains(i)

    p = (1.0 - exp(-k * (n + 0.5) / (m - 1))) ** k
    print(100.0 * p, '%')

    N = 100000
    false_pos = sum(b.contains(i) for i in xrange(n, n + N))
    print(100.0 * false_pos / N, '%')


if __name__ == '__main__':
    test_bloom(50000, 6, 5000)
