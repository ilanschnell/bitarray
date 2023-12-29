from time import perf_counter

from bitarray import bitarray, get_default_endian
from bitarray.util import urandom, ba2hex, hex2ba


# ----- conversion using prefix codes

CODEDICT = {'little': {}, 'big': {
    '0': bitarray('0000'),    '1': bitarray('0001'),
    '2': bitarray('0010'),    '3': bitarray('0011'),
    '4': bitarray('0100'),    '5': bitarray('0101'),
    '6': bitarray('0110'),    '7': bitarray('0111'),
    '8': bitarray('1000'),    '9': bitarray('1001'),
    'a': bitarray('1010'),    'b': bitarray('1011'),
    'c': bitarray('1100'),    'd': bitarray('1101'),
    'e': bitarray('1110'),    'f': bitarray('1111'),
}}
for k, v in CODEDICT['big'].items(): # type: ignore
    CODEDICT['little'][k] = v[::-1]  # type: ignore

def prefix_ba2hex(a):
    return ''.join(a.iterdecode(CODEDICT[a.endian()]))

def prefix_hex2ba(s, endian=None):
    a = bitarray(0, endian or get_default_endian())
    a.encode(CODEDICT[a.endian()], s)
    return a

# ----- test

def test_round(f, g, n, endian):
    # f: function which takes bitarray and returns hexstr
    # g: function which takes hexstr and returns bitarray
    # n: size of random bitarray
    a = urandom(n, endian)
    t0 = perf_counter()
    s = f(a)
    print('%s:  %6.3f ms' % (f.__name__, 1000.0 * (perf_counter() - t0)))
    t0 = perf_counter()
    b = g(s, endian)
    print('%s:  %6.3f ms' % (g.__name__, 1000.0 * (perf_counter() - t0)))
    assert b == a

if __name__ == '__main__':
    n = 100_000_004
    for endian in 'little', 'big':
        print('%s-endian:' % endian)
        for f in ba2hex, prefix_ba2hex:
            for g in hex2ba, prefix_hex2ba:
                test_round(f, g, n, endian)
        print()
