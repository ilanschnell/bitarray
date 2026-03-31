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
        a = bitarray('1' * 1000)
        self.assertEqual(len(a), 1000)
        _testcapi.set_nomemory(5, 0)  # fail from 5th allocation
        try:
            a.extend(bitarray('0' * 10_000_000))
            _testcapi.remove_mem_hooks()
        except MemoryError:
            _testcapi.remove_mem_hooks()
            try:
                a[0]  # used to segfault — ob_item is NULL
            except IndexError:
                got_here = True

        self.assertTrue(got_here)
        self.assertEqual(len(a), 0)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    unittest.main()
