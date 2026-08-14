"""
This test file contains unittests which depend on the _testapi module in
the standard library, as _testapi is not officially supported, and many
not be available on all CPython builds.
"""
import unittest
import _testcapi

from bitarray import bitarray


class Tests(unittest.TestCase):

    def test_finding4(self):
        got_here = False
        a = bitarray('10101010' * 1000)
        a.buffer_info()  # warmup cached namedtuple
        _testcapi.set_nomemory(1, 0)  # fail all allocations from 1st onward
        try:
            a.buffer_info()  # used to segfault
            _testcapi.remove_mem_hooks()
        except MemoryError:
            _testcapi.remove_mem_hooks()
            got_here = True

        self.assertTrue(got_here)

    def test_finding9(self):
        got_here = False
        a = 1000 * bitarray('1')
        b = 10_000_000 * bitarray('0')

        _testcapi.set_nomemory(1, 0)
        try:
            a.extend(b)
        except MemoryError:
            got_here = True
        finally:
            _testcapi.remove_mem_hooks()

        self.assertTrue(got_here)
        self.assertEqual(a, 1000 * bitarray('1'))

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
