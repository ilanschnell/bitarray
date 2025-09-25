import unittest

from bitarray import bitarray


PATTERN = [0, 1, 4, 8, 16, 24, 32, 40, 48, 56, 64, 76, 88, 100, 112, 124, 136]

def get_alloc(a):
    info = a.buffer_info()
    return info.alloc

def resize(a, n):
    increase = n - len(a)
    if increase > 0:
        a.extend(bitarray(increase))
    elif increase < 0:
        del a[n:]

def show(a):
    info = a.buffer_info()
    print('%d  %d' % (info.nbytes, info.alloc))


class ResizeTests(unittest.TestCase):

    def test_pattern(self):
        pat = []
        a = bitarray()
        prev = -1
        while len(a) < 1000:
            alloc = get_alloc(a)
            if prev != alloc:
                pat.append(alloc)
            prev = alloc
            a.append(0)
        self.assertEqual(pat, PATTERN)

    def test_increase(self):
        # make sure sequence of appends will always increase allocated size
        a = bitarray()
        prev = -1
        while len(a) < 100_000:
            alloc = get_alloc(a)
            self.assertTrue(prev <= alloc)
            prev = alloc
            a.append(1)

    def test_decrease(self):
        # ensure that when we start from a large array and delete part, we
        # always get a decreasing allocation
        a = bitarray(10_000_000)
        prev = get_alloc(a)
        while a:
            del a[-100_000:]
            alloc = get_alloc(a)
            self.assertTrue(alloc <= prev)
            prev = alloc

    def test_no_overalloc(self):
        # initalizing a bitarray does not overallocate
        for n in range(1000):
            a = bitarray(8 * n * [1])
            self.assertEqual(get_alloc(a), n)
            b = bitarray(a)
            self.assertEqual(get_alloc(b), n)
            for c in [bitarray(8 * n), bitarray(n * b'A'),
                      bitarray(bytearray(n * b'A'))]:
                self.assertEqual(get_alloc(c), n)

    def test_no_overalloc_large(self):
        # starting from a large bitarray, make we sure we don't realloc each
        # time we extend
        a = bitarray(1_000_000)  # no overallocation
        self.assertEqual(get_alloc(a), 125_000)
        a.extend(bitarray(8))  # overallocation happens here
        alloc = get_alloc(a)
        for _ in range(1000):
            a.extend(bitarray(8))
            self.assertEqual(get_alloc(a), alloc)

if __name__ == '__main__':
    unittest.main()
