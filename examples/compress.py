"""
Demonstrates how the bz2 module may be used to create a compressed object
which represents a bitarray.
"""
import bz2

from bitarray import bitarray


def compress(ba):
    """
    Given a bitarray, return an object which represents all information
    within the bitarray in a compresed form.
    The function `decompress` can be used to restore the bitarray from the
    compresed object.
    """
    assert isinstance(ba, bitarray)
    return ba.length(), bz2.compress(ba.tobytes()), ba.endian()


def decompress(obj):
    """
    Given an object (created by `compress`), return the a copy of the
    original bitarray.
    """
    n, data, endian = obj
    res = bitarray(endian=endian)
    res.frombytes(bz2.decompress(data))
    del res[n:]
    return res


if __name__ == '__main__':
    a = bitarray(12345)
    a.setall(0)
    a[::10] = True
    c = compress(a)
    print c
    b = decompress(c)
    assert a == b, a.endian() == b.endian()
