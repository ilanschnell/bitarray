# variable length format
from bitarray import bitarray, get_default_endian


# 1xxx0000 10000000 00000000

def decode(stream, endian=None):
    a = bitarray(0, 'big')
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
    if endian is None:
        endian = get_default_endian()
    return a if endian == 'big' else bitarray(a, 'little')


def encode(a):
    n = 1 + ((len(a) + 3 - 1) // 7)  # number of resulting bytes
    m = 7 * n - 3                    # number of bits resulting bytes can hold
    unused = m - len(a)
    assert 0 <= unused < 7
    res = bitarray(0, 'big')
    res.append(len(a) > 4)
    for x in 4, 2, 1:
        res.append(bool(x & unused))
    res.extend(a[:4])
    i = 4
    while i < len(a):
        res.append(i + 7 < m)   # leading bit
        res.extend(a[i:i + 7])
        i += 7
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
                (b'\x00', '0000'),
                (b'\x01', '0001'),
                (b'\xe0\x40', '0000 1'),
                (b'\x90\x02', '0000 000001'),
                (b'\xa8\x80\x04', '1000 0000000 00001'),
                (b'\x88\x80\x04', '1000 0000000 0000100'),
                (b'\xe8\x80\x84\x40', '1000 0000000 0000100 1'),
        ]:
            a = bitarray(bits)
            self.assertEqual(encode(a), s)
            self.assertEqual(decode(iter(s)), a)

    def test_random(self):
        for endian in 'big', 'little':
            for n in range(1000):
                a = urandom(n, endian=endian)
                s = encode(a)
                self.assertEqual(len(s), 1 + ((len(a) + 3 - 1) // 7))
                b = decode(iter(s), endian)
                self.assertEqual(b, a)
                self.assertEqual(b.endian(), endian)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
