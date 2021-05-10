from time import time

from bitarray import bitarray, get_default_endian
from bitarray.util import urandom, ba2hex, hex2ba, ba2base, base2ba


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
for k, v in CODEDICT['big'].items():
    CODEDICT['little'][k] = v[::-1]

def prefix_ba2hex(a):
    return ''.join(a.iterdecode(CODEDICT[a.endian()]))

def prefix_hex2ba(s, endian=None):
    a = bitarray(0, endian or get_default_endian())
    a.encode(CODEDICT[a.endian()], s)
    return a

# ----- conversion using ba2base (element based utility function)

def ba2_base16(a):
    return ba2base(16, a)

def base16_2ba(s, endian=None):
    return base2ba(16, s, endian)

# ----- test

def test_round(f, g, n, endian):
    # f: function which takes bitarray and returns hexstr
    # g: function which takes hexstr and returns bitarray
    # n: size of random bitarray
    a = urandom(n, endian)
    t0 = time()
    s = f(a)
    print('%s:  %9.6f sec' % (f.__name__, time() - t0))
    t0 = time()
    b = g(s, endian)
    print('%s:  %9.6f sec' % (g.__name__, time() - t0))
    assert b == a

if __name__ == '__main__':
    n = 100 * 1000 * 1000 + 4
    for endian in 'little', 'big':
        print('%s-endian:' % endian)
        for f in ba2hex, ba2_base16, prefix_ba2hex:
            for g in hex2ba, base16_2ba, prefix_hex2ba:
                test_round(f, g, n, endian)
            print()
