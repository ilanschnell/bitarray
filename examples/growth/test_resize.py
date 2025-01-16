import unittest

from bitarray import bitarray


def get_alloc(a):
    return a.buffer_info()[4]

def show(a):
    info = a.buffer_info()
    size = info[1]
    alloc = info[4]
    print('%d  %d' % (size, alloc))


class ResizeTests(unittest.TestCase):

    def test_increase(self):
        # make sure sequence of appends will always increase allocated size
        a = bitarray()
        prev = -1
        while len(a) < 1_000_000:
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
            alloc = a.buffer_info()[4]
            self.assertTrue(alloc <= prev)
            prev = alloc

    def test_no_overalloc(self):
        # initalizing a bitarray from a list or bitarray does not overallocate
        for n in range(1000):
            a = bitarray(8 * n * [1])
            self.assertEqual(get_alloc(a), n)
            b = bitarray(a)
            self.assertEqual(get_alloc(b), n)
            c = bitarray(8 * n)
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
