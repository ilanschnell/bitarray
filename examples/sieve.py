# Exmaple from  stevech1097@yahoo.com.au

def primesToN1(n):
    import numpy
    # use numpy: 8-bit array of boolean flags
    if n < 2:
        return []
    A = numpy.ones(n+1, numpy.bool) # set to 1 == True
    A[2*2::2] = 0
    for i in xrange(3, int(n**.5)+1, 2): # odd numbers
        if A[i]:  # i is prime
            A[i*i::i*2] = 0
    return numpy.flatnonzero(A)[2:] # 0 and 1 are not prime


def primesToN2(n):
    import bitarray
    # use bitarray: 1-bit boolean flags
    if n < 2:
        return []
    A = bitarray.bitarray(n+1)
    A.setall(1)
    A[:2] = A[2*2::2] = False
    for i in xrange(3, int(n**.5)+1, 2): # odd numbers
        if A[i]:  # i is prime
            A[i*i::i*2] = False
    return A.search('1')


print primesToN2(10000000)[:10]
