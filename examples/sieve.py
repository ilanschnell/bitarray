"""
Demonstrates the implementation of "Sieve of Eratosthenes" algorithm for
finding all prime numbers up to any given limit.
"""
from __future__ import print_function
import sys
from math import sqrt
if sys.version_info[0] == 2:
    range = xrange

from bitarray import bitarray
from bitarray.util import count_n


MILLION = 1000 * 1000
N = 100 * MILLION

# Each bit corresponds to whether or not a[i] is a prime
a = bitarray(N + 1)
a.setall(True)
# Zero and one are not prime
a[:2] = False
# Perform sieve
for i in range(2, int(sqrt(N)) + 1):
    if a[i]:  # i is prime, so all multiples are not
        a[i*i::i] = False

print('the first few primes are:')
print(a.search(1, 20))

# There are 5,761,455 primes up to 100 million
print('there are %d primes up to %d' % (a.count(), N))

# The number of twin primes up to 100 million is 440,312
print('number of twin primes up to %d is %d' %
      (N, len(a.search(bitarray('101')))))

# The 1 millionth prime number is 15,485,863
m = MILLION
print('the %dth prime is %d' % (m, count_n(a, m) - 1))
