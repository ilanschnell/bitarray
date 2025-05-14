from random import getrandbits, randint, randrange
import unittest

from bitarray import bitarray
from bitarray.util import urandom


def nxir(x, start, step):
    assert x >= start and step > 0
    # in Python we can use a simler expression than in C
    return (start - x) % step


def set_range_opt(self, start, stop, step, value):
    ca = (start + 7) // 8
    cb = stop // 8
    m = (cb - ca) * 8

    assert m >= 0
    
    mask = bitarray(step, self.endian)
    mask.setall(not value)
    mask[nxir(8 * ca, start, step)] = value
    mask *= (m - 1) // step + 1
    del mask[m:]  # in the C version we don't bother
    assert len(mask) % 8 == 0

    self[start : 8 * ca : step] = value
    if value:
        self[8 * ca : 8 * cb] |= mask
    else:
        self[8 * ca : 8 * cb] &= mask
    self[8 * cb + nxir(8 * cb, start, step) : stop : step] = value


class Tests(unittest.TestCase):

    def test_setslice_bool_step(self):
        # this test exercises set_range() when stop is much larger than start
        for _ in range(5000):
            n = randrange(3000, 4000)
            a = urandom(n)
            aa = a.tolist()
            start = randrange(1000)
            s = slice(start, randrange(1000, n), randint(1, 100))
            self.assertTrue(s.stop - s.start >= 0)
            slicelength = len(range(n)[s])
            self.assertTrue(slicelength > 0)
            v = getrandbits(1)
            set_range_opt(a, s.start, s.stop, s.step, v)
            aa[s] = slicelength * [v]
            self.assertEqual(a.tolist(), aa)
    

if __name__ == '__main__':
    unittest.main()
