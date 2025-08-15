"""
Demonstrates the implementation of "Sieve of Eratosthenes" algorithm for
finding all prime numbers up to any given limit.

It should be noted that bitarray version 3.7 added util.gen_primes().
The library function basically uses an optimized version of the same
algorithm.
"""
from math import isqrt

from bitarray import bitarray
from bitarray.util import ones, count_n, sum_indices


N = 100_000_000

# Each bit a[i] corresponds to whether or not i is a prime
a = ones(N)

# Zero and one are not prime
a[:2] = False

# Perform sieve
for i in range(2, isqrt(N) + 1):
    if a[i]:  # i is prime, so all multiples are not
        a[i*i::i] = False

stop = 50
print('primes up to %d are:' % stop)
print(list(a.search(1, 0, stop)))

# There are 5,761,455 primes up to 100 million
x = a.count()
print('there are {:,d} primes up to {:,d}'.format(x, N))
assert x == 5_761_455 or N != 100_000_000

# The number of twin primes up to 100 million is 440,312
# we need to add 1 as .count() only counts non-overlapping sub_bitarrays
# and (3, 5) as well as (5, 7) are both twin primes
x = a.count(bitarray('101')) + 1
print('number of twin primes up to {:,d} is {:,d}'.format(N, x))
assert x == 440_312 or N != 100_000_000

# The 1 millionth prime number is 15,485,863
m = 1_000_000
x = count_n(a, m) - 1
print('the {:,d}-th prime is {:,d}'.format(m, x))
assert x == 15_485_863

# The sum of all prime numbers below one million is 37,550,402,023.
m = 1_000_000
x = sum_indices(a[:m])
print('the sum of prime numbers below {:,d} is {:,d}'.format(m, x))
assert x == 37_550_402_023
