import binascii
from random import randint

from bitarray import bitarray, bits2bytes
from bitarray.util import urandom


def serialize(a):
    buffer_info = a.buffer_info()
    return 'bitarray%d%s%s' % (buffer_info[3],
                               buffer_info[2][0],
                               binascii.hexlify(a.tobytes()).decode())

def deserialize(s):
    assert s.startswith('bitarray')
    ed = {'l': 'little', 'b': 'big'}
    a = bitarray(endian=ed[s[9]])
    a.frombytes(binascii.unhexlify(s[10:]))
    unused = int(s[8])
    if unused:
        del a[-unused:]
    return a


for n in range(1000):
    a = urandom(n, endian=['little', 'big'][randint(0, 1)])
    s = serialize(a)
    #print(s)
    b = deserialize(s)
    assert a == b
    assert a.endian() == b.endian()
