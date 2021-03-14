import binascii
from random import randint

from bitarray import bitarray
from bitarray.util import urandom


def serialize(a):
    buffer_info = a.buffer_info()
    return 'bty%d%s%s' % (buffer_info[3], buffer_info[2][0],
                          binascii.hexlify(a.tobytes()).decode())

def deserialize(s):
    if not s.startswith('bty'):
        raise ValueError("expected string starting with 'bty'")
    unused = int(s[3])
    ed = {'l': 'little', 'b': 'big'}
    a = bitarray(endian=ed[s[4]])
    a.frombytes(binascii.unhexlify(s[5:]))
    if unused:
        del a[-unused:]
    return a


for n in range(1000):
    a = urandom(n, endian=['little', 'big'][randint(0, 1)])
    s = serialize(a)
    if n < 10:
        print(s)
    b = deserialize(s)
    assert a == b
    assert a.endian() == b.endian()
