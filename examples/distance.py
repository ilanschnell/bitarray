from time import time

from bitarray.util import _correspond_all, count_and, count_xor, urandom

import numpy
import scipy.spatial.distance as distance


def dice(u, v):
    x = float(count_xor(u, v))
    return x / (2.0 * count_and(u, v) + x)

def hamming(u, v):
    return float(count_xor(u, v)) / len(u)

def jaccard(u, v):
    x = float(count_xor(u, v))
    return x / (count_and(u, v) + x)

def kulczynski1(u, v):
    x = count_xor(u, v)
    return float(count_and(u, v)) / x

def rogerstanimoto(u, v):
    nff, nft, ntf, ntt = _correspond_all(u, v)
    R = 2.0 * (ntf + nft)
    return R / (ntt + nff + R)

def russellrao(u, v):
    n = float(len(u))
    return (n - count_and(u, v)) / n

def sokalmichener(u, v):
    nff, nft, ntf, ntt = _correspond_all(u, v)
    R = 2.0 * (ntf + nft)
    return R / (ntt + nff + R)

def sokalsneath(u, v):
    R = 2.0 * count_xor(u, v)
    return R / (count_and(u, v) + R)

def yule(u, v):
    nff, nft, ntf, ntt = _correspond_all(u, v)
    half_R = ntf * nft
    if half_R == 0:
        return 0.0
    else:
        return 2.0 * half_R / (ntt * nff + half_R)


def test(n):
    a = urandom(n)
    b = urandom(n)
    aa = numpy.frombuffer(a.unpack(), dtype=bool)
    bb = numpy.frombuffer(b.unpack(), dtype=bool)

    for name in ['dice', 'hamming', 'jaccard', 'kulczynski1',
                 'rogerstanimoto', 'russellrao', 'sokalmichener',
                 'sokalsneath', 'yule']:
        f1 = eval(name)
        t0 = time()
        x1 = f1(a, b)
        print('%.14f  %9.6f sec  %s' % (x1, time() - t0, name))

        f2 = getattr(distance, name)
        t0 = time()
        x2 = f2(aa, bb)
        print('%.14f  %9.6f sec' % (x2, time() - t0))

        assert abs(x1 - x2) < 1E-14

test(2 ** 25 + 67)
