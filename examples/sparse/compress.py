import bz2
import gzip
from collections import Counter
from itertools import count, islice
from math import ceil, sqrt
from pprint import pprint
from random import random, randint
from time import time

from bitarray import bitarray, bits2bytes
from bitarray.util import count_n, sc_encode, sc_decode


def random_array(n, p=0.1, endian=None):
    return bitarray((random() < p for _ in range(n)), endian)

def stats_bitarray(a):
    res = Counter()
    for offset in count(0, 256):
        n = min(len(a) - offset, 256)
        last = n < 256 or offset + 256 == len(a)
        b = a[offset:offset + n]
        cnt = b.count()
        if cnt >= bits2bytes(n):  # raw block
            res['raw'] += 1
        else:
            res[cnt] += 1
        if last:
            break
    return res

def stats_bytes(stream):
    cnt = Counter()
    stream = iter(stream)
    endian = 'big' if next(stream) == 66 else 'little'
    last_bits = next(stream)
    nbits = 0
    while True:
        head = next(stream)
        last = bool(head & 0x80)
        raw = bool(head & 0x40)
        k = head & 0x3f
        bytes(islice(stream, k))
        if raw:
            nbits += 8 * k
            cnt['raw'] += 1
        else:
            nbits += 256
            cnt[k] += 1
        if last:
            break

    if last_bits:
        assert nbits > 0
        full_blocks = (nbits - 1) // 256
        nbits = 256 * full_blocks + last_bits

    return cnt, endian, nbits

def test_stats():
    for n in range(1000):
        a = random_array(n, 0.2 * random(), ['little', 'big'][randint(0, 1)])
        b = sc_encode(a)
        c = sc_decode(b)
        assert a == c
        assert a.endian() == c.endian()
        stats_a = stats_bitarray(a)
        stats_b = stats_bytes(b)
        for cnt in range(33):
            assert stats_a[cnt] == stats_b[0][cnt]
        assert stats_b[1] == c.endian()
        assert stats_b[2] == n

def sieve_example():
    MILLION = 1000 * 1000
    N = 100 * MILLION

    a = bitarray(N)
    a.setall(True)
    a[:2] = False
    for i in range(2, ceil(sqrt(N))):
        if a[i]:
            a[i*i::i] = False

    print(100.0 * a.count() / len(a))
    b = sc_encode(a)
    print(100.0 * len(b) / bits2bytes(N))
    c = sc_decode(b)
    assert a == c
    m = MILLION
    assert(count_n(c, m) - 1 == 15_485_863)

    cnt = stats_bytes(b)[0]
    pprint(cnt)

def compare():
    a = random_array(1 << 24, 0.01)
    raw = a.tobytes()
    print("               compress (ms)   decompress (ms)             ratio")
    print(65 * '-')
    for name, f_e, f_d in [
            ('sc' , sc_encode, sc_decode),
            ('gzip', gzip.compress, gzip.decompress),
            ('bz2', bz2.compress, bz2.decompress)]:
        x = a if name == 'sc' else raw
        t0 = time()
        b = f_e(x)  # compression
        t1 = time()
        c = f_d(b)  # decompression
        t2 = time()
        print("    %-6s  %16.3f  %16.3f  %16.3f" %
              (name, 1000 * (t1 - t0), 1000 * (t2 - t1), len(b) / len(raw)))
        assert c == x

if __name__ == '__main__':
    test_stats()
    sieve_example()
    compare()
