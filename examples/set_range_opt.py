import unittest
from random import getrandbits, randint, randrange
from time import perf_counter

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
    assert 0 <= 8 * ca - start < 8
    assert 0 <= stop - 8 * cb < 8

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

    def test_nxir(self):
        for _ in range(1000):
            start = randrange(100)
            step = randrange(1, 20)
            x = randrange(start, start + 100)
            nx = nxir(x, start, step)
            self.assertTrue(0 <= nx < step)
            self.assertEqual((x + nx) % step, start % step)

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


def speed_cmp():
    n = 1_000_000
    print("n=%d\ntimes in micro seconds\n" % n)
    print('%8s %12s %12s' % ("step", "this-code", "native"))
    for step in range(1, 20):
        a = bitarray(n)
        b = bitarray(n)
        t0 = perf_counter()
        set_range_opt(a, 0, n, step, 1)
        t1 = perf_counter()
        b[::step] = 1
        t2 = perf_counter()
        print('%8d %12.3f %12.3f' % (step, 1E6 * (t1 - t0), 1E6 * (t2 - t1)))
        assert a == b


if __name__ == '__main__':
    speed_cmp()
    unittest.main()
