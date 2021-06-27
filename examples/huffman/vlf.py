# variable length format
from bitarray import bitarray, get_default_endian
from bitarray.util import make_endian

# 1xxx0000 10000000 00000000

def decode(stream, endian=None):
    a = bitarray(endian='big')
    b = next(stream)
    unused = (b & 0x7f) >> 4
    assert 0 <= unused < 7
    a.frombytes(bytes([b]))
    del a[:4]
    while b & 0x80:
        b = next(stream)
        a.frombytes(bytes([b]))
        del a[-8:-7]
    if unused:
        del a[-unused:]
    return make_endian(a, get_default_endian() if endian is None else endian)


def encode(a):
    n = 1 + ((len(a) + 3 - 1) // 7)
    m = 7 * n - 3
    unused = m - len(a)
    assert 0 <= unused < 7
    res = bitarray(0, 'big')
    res.append(len(a) > 4)
    for x in 4, 2, 1:
        res.append(bool(x & unused))
    res.extend(a[:4])
    i = 4
    while i < len(a):
        res.append(i + 7 < m)
        res.extend(a[i:i + 7])
        i += 7
    assert 8 * (n - 1) < len(res) <= 8 * n
    return res.tobytes()

# ---------------------------------------------------------------------------

import unittest

from bitarray.util import urandom


class VLFTests(unittest.TestCase):

    def test_explicit(self):
        for s, bits in [
                (b'\x40', ''),
                (b'\x30', '0'),
                (b'\x38', '1'),
                (b'\x01', '0001'),
                (b'\xe0\x40', '0000 1'),
                (b'\x90\x02', '0000 000001'),
                (b'\xa8\x80\x04', '1000 0000000 00001'),
                (b'\x88\x80\x04', '1000 0000000 0000100'),
                (b'\xe8\x80\x84\x40', '1000 0000000 0000100 1'),
        ]:
            a = decode(iter(s))
            self.assertEqual(a, bitarray(bits))
            t = encode(a)
            self.assertEqual(t, s)

    def test_random(self):
        for endian in 'big', 'little':
            for n in range(1000):
                a = urandom(n, endian=endian)
                s = encode(a)
                b = decode(iter(s), endian)
                self.assertEqual(b, a)
                self.assertEqual(b.endian(), endian)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
