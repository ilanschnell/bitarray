import bz2
import gzip
from time import perf_counter

from bitarray.util import (
    ones, urandom,
    serialize, deserialize,
    sc_encode, sc_decode,
    vl_encode, vl_decode,
)

from sc_stat import sc_stat


def p_range():
    n = 1 << 28
    p = 1.0
    a = ones(n)
    print("        p          ratio         raw"
          "    type 1    type 2    type 3    type 4")
    print("   " + 73 *'-')
    while p > 1e-8:
        b = sc_encode(a)
        blocks = sc_stat(b)['blocks']
        print('  %11.8f  %11.8f  %8d  %8d  %8d  %8d  %8d' %
              tuple([p, len(b) / (n / 8)] + blocks))
        assert a == sc_decode(b)
        a &= urandom(n)
        p /= 2

def compare():
    n = 1 << 26
    # create random bitarray with p = 1 / 2^9 = 1 / 512 = 0.195 %
    a = ones(n)
    for i in range(10):
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
        t0 = perf_counter()
        b = f_e(x)  # compression
        t1 = perf_counter()
        c = f_d(b)  # decompression
        t2 = perf_counter()
        print("    %-11s  %16.3f  %16.3f  %16.4f" %
              (name, 1000 * (t1 - t0), 1000 * (t2 - t1), len(b) / len(raw)))
        assert c == x

if __name__ == '__main__':
    compare()
    p_range()
