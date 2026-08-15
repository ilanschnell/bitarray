import unittest
from random import randrange

from bitarray._util import leb128_encode, leb128_decode


class LEB128_Tests(unittest.TestCase):

    def test_example(self):
        i = 624485
        b = [0xe5, 0x8e, 0x26]
        self.assertEqual(leb128_encode(i), bytes(b))
        self.assertEqual(leb128_decode(b), i)

    def test_range(self):
        for i in range(1000):
            b = leb128_encode(i)
            self.assertEqual(leb128_decode(b), i)

    def test_random(self):
        for _ in range(10000):
            i = randrange(1_000_000_000)
            b = leb128_encode(i)
            self.assertEqual(leb128_decode(b), i)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
