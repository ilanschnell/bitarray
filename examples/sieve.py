import time

import numpy
import bitarray

def primesToN1(n):
    # use numpy: 8-bit array of boolean flags
    if n < 2:
        return []
    print 'init numpy'
    A = numpy.ones(n+1, numpy.bool) # set to 1 == True
    A[:2] = A[2*2::2] = 0
    print 'sieve'
    for i in xrange(3, int(n**.5)+1, 2): # odd numbers
        if A[i]:  # i is prime
            A[i*i::i*2] = 0
    print 'counting'
    print numpy.sum(A)


def primesToN2(n):
    # use bitarray: 1-bit boolean flags
    if n < 2:
        return []
    print 'init bitarray'
    A = bitarray.bitarray(n+1)
    A.setall(1)
    A[:2] = A[2*2::2] = 0
    print 'sieve'
    for i in xrange(3, int(n**.5)+1, 2): # odd numbers
        if A[i]:  # i is prime
            A[i*i::i*2] = 0
    print 'counting'
    print A.count()


N = 100 * 1000 * 1000

def run(func):
    start_time = time.time()
    func(N)
    print 'time: %.6f sec\n' % (time.time() - start_time)

run(primesToN1)
run(primesToN2)
