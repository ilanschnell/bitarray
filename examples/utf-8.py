from bitarray import bitarray
from bitarray.util import ba2int, pprint


# See: https://en.wikipedia.org/wiki/UTF-8

def code_point(u):
    print('character:', u)
    b = u.encode('utf-8')
    print('hexadecimal:', ' '.join('%02x' % i for i in b))
    a = bitarray(b, endian='big')
    pprint(a)

    # calculate binary code point from binary UTF-8 representation
    if a[0:1] == bitarray('0'):
        c = a[1:8]
        assert len(a) == 8
    elif a[0:3] == bitarray('110'):
        c = a[3:8] + a[10:16]
        assert a[8:10] == bitarray('10')
        assert len(a) == 16
    elif a[0:4] == bitarray('1110'):
        c = a[4:8] + a[10:16] + a[18:24]
        assert a[8:10] == a[16:18] == bitarray('10')
        assert len(a) == 24
    elif a[0:5] == bitarray('11110'):
        c = a[5:8] + a[10:16] + a[18:24] + a[26:32]
        assert a[8:10] == a[16:18] == a[24:26] == bitarray('10')
        assert len(a) == 32
    else:
        raise
    code_point = ba2int(c)

    print('code point:', hex(code_point))
    print()


for u in '\u0024 \u00a2 \u20ac \ud55c \U00010348 \U0010ffff'.split():
    code_point(u)
