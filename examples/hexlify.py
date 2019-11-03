import binascii
from bitarray import bitarray


def hexlify(a):
    "Hexadecimal representation of bitarray (with length multiple of 4)"
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected")
    la = len(a)
    if la % 4:
        raise ValueError("bitarray length not multiple of 4")
    if la % 8:
        # make sure we don't mutate the original argument
        a = a + bitarray(4)
    assert len(a) % 8 == 0
    s = binascii.hexlify(a.tobytes())
    if la % 8:
        s = s[:-1]
    return s


def unhexlify(s):
    "Bitarray of hexadecimal representation"
    if not isinstance(s, (str, bytes)):
        raise TypeError("string expected")
    ls = len(s)
    if ls % 2:
        s = s + ('0' if isinstance(s, str) else b'0')
    assert len(s) % 2 == 0
    a = bitarray()
    a.frombytes(binascii.unhexlify(s))
    if ls % 2:
        del a[-4:]
    return a


def test_conversion():
    import sys
    from string import hexdigits
    from random import choice, randint

    is_py3k = bool(sys.version_info[0] == 3)

    def check_round_trip(s):
        t = hexlify(unhexlify(s))
        assert t.decode() == s.lower()

    for i in range(100):
        s = ''.join(choice(hexdigits) for _ in range(randint(0, 1000)))
        check_round_trip(s)

    for h, sa in [('',    ''),           ('0',   '0000'),
                  ('a',   '1010'),       (b'f',  '1111'),
                  ('1a',  '00011010'),   (b'2B', '00101011'),
                  ('F7E', '111101111110')]:
        a = bitarray(sa)
        assert unhexlify(h) == a

        t = hexlify(a)
        # make sure we hexlify hasn't changed its input argument
        assert a == bitarray(sa)
        if is_py3k:
            t = t.decode()
        if isinstance(h, bytes):
            h = h.decode()
        h = h.lower()
        assert t == h, '%r!=%r' % (t, h)

    a = bitarray('1110')
    assert hexlify(a) == b'e'
    for x in 'e', 'E', b'e', b'E':
        assert unhexlify(x) == a


if __name__ == '__main__':
    test_conversion()
