import binascii
from random import randint

from bitarray import bitarray
from bitarray.util import urandom, _is_py2


def serialize(a):
    """serialize(bitarray, /) -> str

Return a serialized string representation of the bitarray.  The serialized
string contains only ASCII letters and numbers, and can be passed as input
to `deserialize()`.  It compactly represents the bitarray object (including
its endianness) and is guaranteed not to change in future versions.
"""
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected")
    buffer_info = a.buffer_info()
    return '%d%s%s' % (buffer_info[3], buffer_info[2][0],
                       binascii.hexlify(a.tobytes()).decode())

def deserialize(s):
    """deserialize(string, /) -> bitarray

Return a bitarray given a serialized string (returned by `serialized()`).
"""
    if not isinstance(s, (str, unicode) if _is_py2 else str):
        raise TypeError("str expected, got: %s" % type(s))
    unused = int(s[0])
    ed = {'l': 'little', 'b': 'big'}
    a = bitarray(endian=ed[s[1]])
    a.frombytes(binascii.unhexlify(s[2:]))
    if unused:
        del a[-unused:]
    return a


for n in range(1000):
    a = urandom(n, endian=['little', 'big'][randint(0, 1)])
    s = serialize(a)
    if n % 50 == 0:
        print(s)
    b = deserialize(s)
    assert a == b
    assert a.endian() == b.endian()
