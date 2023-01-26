# Useful functions related to sparse bitarray compression.
# In particular the function sc_stats() which returns the
# frequency of each block type.

import bz2
import gzip
import sys
from time import time
from collections import Counter
from itertools import islice
from random import random, randint

from bitarray import bitarray
from bitarray.util import (
    zeros, urandom,
    serialize, deserialize,
    sc_encode, sc_decode,
    vl_encode, vl_decode,
)

def read_n(n, stream):
    i = 0
    for j in range(n):
        c = next(stream)
        i |= c << 8 * j
    if i < 0:
        raise ValueError("read %d bytes got negative value: %d" % (n, i))
    return i

def sc_decode_header(stream):
    head = next(stream)
    endian = 'big' if head & 0x10 else 'little'
    length = head & 0x0f
    if head & 0xe0 or length > 8:
        raise ValueError("invalid header: 0x%02x" % head)

    nbits = read_n(length, stream)
    return endian, nbits

def sc_decode_block(stream, stats):
    head = next(stream)
    if head == 0:  # stop byte
        return False

    if head <= 128:
        n = 0
        k = head
        assert 0 < k <= 128
    elif 160 <= head < 192:
        n = 1
        k = head - 160
        assert k < 32
    elif 192 <= head <= 194:
        n = head - 190
        k = next(stream)
    else:
        raise ValueError("Invalid block head: 0x%02x" % head)

    stats['blocks'][n] += 1

    # consume block data
    size = max(1, n) * k        # size of block data
    next(islice(stream, size, size), None)

    return True

def sc_stats(stream):
    """sc_stats(stream) -> dict

Decode a compressed byte stream (generated by `sc_encode()` and return
useful statistics.  In particular, the frequency of each block type.
"""
    stream = iter(stream)
    endian, nbits = sc_decode_header(stream)

    stats = {
        'endian': endian,
        'nbits': nbits,
        'blocks': Counter()
    }

    while sc_decode_block(stream, stats):
        pass

    stop = False
    try:
        next(stream)
    except StopIteration:
        stop = True
    assert stop

    return stats

def test_sc_stat():
    a = bitarray(1<<33, 'little')
    a.setall(0)
    a[:1<<16] = 1
    a[:1<<18:1<<4] = 1
    a[:1<<22:1<<12] = 1
    a[:1<<30:1<<20] = 1
    assert a.count() == 79804
    b = sc_encode(a)
    stat = sc_stats(b)
    assert stat['endian'] == 'little'
    assert stat['nbits'] == 1 << 33
    blocks = stat['blocks']
    for i, n in enumerate([64, 754, 46, 48, 2]):
        print("         block type %d  %8d" % (i, blocks[i]))
        assert blocks[i] == n
    if sys.version_info[:2] >= (3, 10):
        print("total number of blocks %8d" % blocks.total())
    assert a == sc_decode(b)

def test_raw_block_size():
    for n in range(10_000):
        a = bitarray(n)
        a.setall(1)
        b = sc_encode(a)
        stat = sc_stats(b)
        assert stat['nbits'] == n
        blocks = stat['blocks']
        assert blocks[0] == (n + 1023) // 1024
        assert sc_decode(b) == a

def random_array(n, p=0.5):
    """random_array(n, p=0.5) -> bitarray

Generate random bitarray of length n.
Each bit has a probability p of being 1.
"""
    if p < 0.05:
        # when the probability p is small, it is faster to randomly
        # set p * n elements
        a = zeros(n)
        for _ in range(int(p * n)):
            a[randint(0, n - 1)] = 1
        return a

    return bitarray((random() < p for _ in range(n)))

def test_random_array():
    n = 10_000_000
    p = 1e-6
    while p < 1.0:
        a = random_array(n, p)
        cnt = a.count()
        print("%10.7f  %10.7f  %10.7f" % (p, cnt / n, abs(p - cnt / n)))
        p *= 1.4

def p_range():
    n = 1 << 24
    p = 1e-6
    print("     p         ratio         raw    type 1    type 2    type 3")
    print(65 *'-')
    while p < 1.0:
        a = random_array(n, p)
        b = sc_encode(a)
        blocks = sc_stats(b)['blocks']
        print('%10.7f  %10.7f  %8d  %8d  %8d  %8d' % (
            p, len(b) / (n / 8), blocks[0], blocks[1], blocks[2], blocks[3]))
        assert a == sc_decode(b)
        p *= 1.6

def compare():
    n = 1 << 24
    # create random bitarray with p = 1 / 2^9 = 1 / 512 = 0.195 %
    a = bitarray(n)
    a.setall(1)
    for i in range(9):
        a &= urandom(n)

    raw = a.tobytes()
    print(20 * ' ' +  "compress (ms)   decompress (ms)             ratio")
    print(70 * '-')
    for name, f_e, f_d in [
            ('serialize', serialize, deserialize),
            ('vl', vl_encode, vl_decode),
            ('sc' , sc_encode, sc_decode),
            ('gzip', gzip.compress, gzip.decompress),
            ('bz2', bz2.compress, bz2.decompress)]:
        x = a if name in ('serialize', 'vl', 'sc') else raw
        t0 = time()
        b = f_e(x)  # compression
        t1 = time()
        c = f_d(b)  # decompression
        t2 = time()
        print("    %-11s  %16.3f  %16.3f  %16.4f" %
              (name, 1000 * (t1 - t0), 1000 * (t2 - t1), len(b) / len(raw)))
        assert c == x

if __name__ == '__main__':
    #test_sc_stat()
    test_raw_block_size()
    #test_random_array()
    p_range()
    compare()
