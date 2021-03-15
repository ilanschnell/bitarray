import binascii
from random import randint

from bitarray import bitarray
from bitarray.util import urandom, _is_py2


def serialize(a):
    """serialize(bitarray, /) -> str

Return a serialized string representation of the bitarray.  The serialized
string containing only hexadecimal numbers, and can be passed as input
to `deserialize()`.  It compactly represents the bitarray object (including
its endianness) and is guaranteed not to change in future versions.
"""
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected")
    info = a.buffer_info()
    return '%d%d%s' % (
        int(info[2] == 'big'),  # 0: little endian, 1: big endian
        info[3],                # number of unused bits in last byte
        binascii.hexlify(a.tobytes()).decode()  # buffer as hexadecimals
    )


def deserialize(s):
    """deserialize(string, /) -> bitarray

Return a bitarray given a serialized string (returned by `serialize()`).
"""
    if not isinstance(s, (str, unicode) if _is_py2 else str):
        raise TypeError("str expected, got: %s" % type(s))
    a = bitarray(endian=['little', 'big'][int(s[0])])
    unused = int(s[1])
    a.frombytes(binascii.unhexlify(s[2:]))
    if unused:
        del a[-unused:]
    return a


def test():
    for n in range(1000):
        a = urandom(n, endian=['little', 'big'][randint(0, 1)])
        s = serialize(a)
        if n < 10 or n % 250 == 0:
            print(s)
        b = deserialize(s)
        assert a == b
        assert a.endian() == b.endian()


if __name__ == '__main__':
    test()
