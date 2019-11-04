import sys

from bitarray import bitarray

is_py3k = bool(sys.version_info[0] == 3)


def int2ba(i):
    "convert the given integer into a bitarray (with no leading zeros)"
    if not isinstance(i, int if is_py3k else (int, long)):
        raise TypeError("integer expected, got: %r" % i)
    if i < 0:
        raise ValueError("non-negative integer expected")
    if i == 0:
        return bitarray("0")
    b = bytearray()
    while i:
        i, r = divmod(i, 256)
        b.append(r)
    b.reverse()
    a = bitarray()
    a.frombytes(bytes(b))
    return a[a.index(1):]


def ba2int(a):
    "convert the given bitarray into an integer"
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected, got: %r" % a)
    if len(a) == 0:
        raise ValueError("non-empty bitarray expected")
    # pad with leadind zeros, such that length is multiple of 8
    b = bitarray((8 - len(a) % 8) * '0') + a
    assert len(b) % 8 == 0
    res, m = 0, 1
    c = bytearray(b.tobytes())
    c.reverse()
    for x in c:
        res += x * m
        m *= 256
    return res


def test_conversion():
    from random import randint

    def check_round_trip(i):
        a = int2ba(i)
        assert len(a) > 0
        # ensure we have no leading zeros
        assert len(a) == 1 or a.index(1) == 0
        assert ba2int(a) == i
        a = bitarray(randint(0, 3) * '0') + a
        assert ba2int(a) == i

    for i in range(1000):
        check_round_trip(i)
        check_round_trip(randint(0, 10 ** randint(3, 300)))

    assert int2ba(25) == bitarray('11001')
    assert int2ba(0) == bitarray('0')
    assert ba2int(bitarray('100001001')) == 265
    assert ba2int(bitarray('0001001')) == 9
    assert ba2int(bitarray('0000')) == 0


if __name__ == '__main__':
    test_conversion()
