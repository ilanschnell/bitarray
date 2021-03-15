import binascii

from bitarray import bitarray
from bitarray.util import urandom, _is_py2


def serialize(a):
    """serialize(bitarray, /) -> bytes

Return a serialized representation of the bitarray, which may be passed
to `deserialize()`.  It compactly represents the bitarray object (including
its endianness) and is guaranteed not to change in future versions.
"""
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected")
    info = a.buffer_info()
    head = 16 * int(info[2] == 'big') + info[3]
    return (chr(head) if _is_py2 else bytes([head])) + a.tobytes()


def deserialize(b):
    """deserialize(bytes, /) -> bitarray

Return a bitarray given the representation returned by `serialize()`.
"""
    if not isinstance(b, bytes):
        raise TypeError("bytes expected, got: %s" % type(b))
    endian, unused = divmod(ord(b[0]) if _is_py2 else b[0], 16)
    a = bitarray(endian=['little', 'big'][endian])
    a.frombytes(b[1:])
    if unused:
        del a[-unused:]
    return a


def test():
    for n in range(1000):
        for endian in 'little', 'big':
            a = urandom(n, endian)
            s = serialize(a)
            if n < 10 or n % 250 == 0:
                print(binascii.hexlify(s).decode())
            b = deserialize(s)
            assert b == a
            assert b.endian() == endian


if __name__ == '__main__':
    test()
