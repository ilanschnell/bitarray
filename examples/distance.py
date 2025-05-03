"""
In this module, we implement distance functions and compare them to the
corresponding functions in the scipy.spatial.distance module.
The functions using bitarray are typically around 50 to 200 times faster.
"""
from time import perf_counter

from bitarray.util import correspond_all, count_and, count_xor, urandom

import numpy
import scipy.spatial.distance as distance  # type: ignore


def dice(u, v):
    x = count_xor(u, v)
    return x / (2 * count_and(u, v) + x)

def hamming(u, v):
    return count_xor(u, v) / len(u)

def jaccard(u, v):
    x = count_xor(u, v)
    return x / (count_and(u, v) + x)

def kulczynski1(u, v):
    return count_and(u, v) / count_xor(u, v)

def rogerstanimoto(u, v):
    x = count_xor(u, v)
    return 2 * x / (len(u) + x)

def russellrao(u, v):
    n = len(u)
    return (n - count_and(u, v)) / n

def sokalmichener(u, v):
    x = count_xor(u, v)
    return 2 * x / (len(u) + x)

def sokalsneath(u, v):
    R = 2 * count_xor(u, v)
    return R / (count_and(u, v) + R)

def yule(u, v):
    nff, nft, ntf, ntt = correspond_all(u, v)
    half_R = ntf * nft
    if half_R == 0:
        return 0.0
    else:
        return 2 * half_R / (ntt * nff + half_R)


def test(n):
    a = urandom(n)
    b = urandom(n)
    aa = numpy.frombuffer(a.unpack(), dtype=bool)
    bb = numpy.frombuffer(b.unpack(), dtype=bool)

    for name in ['dice', 'hamming', 'jaccard', 'kulczynski1',
                 'rogerstanimoto', 'russellrao', 'sokalmichener',
                 'sokalsneath', 'yule']:

        f1 = eval(name)               # function defined above
        t0 = perf_counter()
        x1 = f1(a, b)
        t1 = perf_counter() - t0
        print('%.14f  %6.3f ms  %s' % (x1, 1000.0 * t1, name))

        f2 = getattr(distance, name)  # scipy.spatial.distance function
        t0 = perf_counter()
        x2 = f2(aa, bb)
        t2 = perf_counter() - t0
        print('%.14f  %6.3f ms  %9.2f' % (x2, 1000.0 * t2, t2 / t1))

        assert abs(x1 - x2) < 1E-14

test(2 ** 20 + 67)
