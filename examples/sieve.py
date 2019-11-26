"""
Demonstrates the implementation of "Sieve of Eratosthenes" algorithm for
finding all prime numbers up to any given limit.
"""
from __future__ import print_function
import sys
if sys.version_info[0] == 2:
    range = xrange

from bitarray import bitarray
from bitarray.util import count_n


N = 100 * 1000 * 1000

# Each bit corresponds to whether or not a[i] is a prime
a = bitarray(N + 1)
a.setall(True)
# Zero and one are not prime
a[:2] = False
# Perform sieve
for i in range(2, int(N ** 0.5) + 1):
    if a[i]:  # i is prime
        a[i*i::i] = False

print('the first few primes are:')
for i in range(30):
    if a[i]:
        print(i)

# There are 5,761,455 primes up to 100 million
print('there are %d primes up to %d' % (a.count(), N))
m = 1000 * 1000
# The 1 millionth prime number is 15,485,863
print('the %dth prime is %d' % (m, count_n(a, m) - 1))
