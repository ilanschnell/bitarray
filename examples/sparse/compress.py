import bz2
import gzip
from collections import Counter
from itertools import count, islice
from math import ceil, sqrt
from pprint import pprint
from random import random, randint
from time import time

from bitarray import bitarray, bits2bytes
from bitarray.util import (
    count_n, urandom,
    sc_encode, sc_decode,
    vl_encode, vl_decode,
    serialize, deserialize,
)

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
    # `last_nbits` is the number of bits added by the last block.
    # The value 0 means that 256 bits are added, unless the last block is
    # raw block with no content (k=0) in which 0 bits are added.  This
    # only happens when we have an encoded an empty bitarray.
    last_nbits = next(stream)
    nbits = 0
    for block_number in count(0):
        head = next(stream)
        last_block = bool(head & 0x80)  # is this the last block?
        raw = bool(head & 0x40)         # is this a raw block?
        k = head & 0x3f      # block size in bytes (without head byte)
        bytes(islice(stream, k))
        if raw:
            if k == 0:   #  empty block
                assert last_block and last_nbits == 0 and block_number == 0

            # all raw block contain data, except maybe the first (this
            # happends when an empty bitarray was encoded and this first
            # block will also be the last block).
            assert k > 0 or (block_number == 0 and last_block)

            if last_nbits:
                assert k == (bits2bytes(last_nbits) if last_block else 32)
            else:
                assert k in ([0, 32] if last_block else [32])

            # all raw blocks contain 32 bytes, except maybe the last block
            assert k == 32 or last_block

            nbits += 8 * k
            cnt['raw'] += 1
        else:
            nbits += 256
            cnt[k] += 1
        if last_block:
            break

    if last_nbits:
        assert nbits > 0
        full_blocks = (nbits - 1) // 256
        nbits = 256 * full_blocks + last_nbits

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
    assert(count_n(a, MILLION) - 1 == 15_485_863)

    print(100.0 * a.count() / len(a))
    b = sc_encode(a)
    print(100.0 * len(b) / bits2bytes(N))
    c = sc_decode(b)
    assert a == c

    cnt = stats_bytes(b)[0]
    pprint(cnt)

def p_range():
    for p in range(21):
        p *= 0.01
        a = random_array(1 << 16, p)
        nbytes = bits2bytes(len(a))
        b = sc_encode(a)
        cnt = stats_bytes(b)[0]
        print('%8.2f  %8.3f  %8d' % (p, len(b) / nbytes, cnt['raw']))
        assert a == sc_decode(b)

def compare():
    n = 1 << 24
    # create random bitarray with p = 1 / 2^6 = 1 / 64 = 1.56%
    a = bitarray(n)
    a.setall(1)
    for i in range(6):
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
        print("    %-11s  %16.3f  %16.3f  %16.3f" %
              (name, 1000 * (t1 - t0), 1000 * (t2 - t1), len(b) / len(raw)))
        assert c == x

if __name__ == '__main__':
    test_stats()
    sieve_example()
    p_range()
    compare()
