import unittest
from random import random

from bitarray import decodetree
from bitarray.test_bitarray import show_info
from bitarray.util import _huffman_tree, huffman_code


class HuffmanTreeTests(unittest.TestCase):

    # tests for _huffman_tree()

    def test_empty(self):
        freq = {}
        self.assertRaises(IndexError, _huffman_tree, freq)

    def test_one_symbol(self):
        freq = {"A": 1}
        tree = _huffman_tree(freq)
        self.assertEqual(tree.symbol, "A")
        self.assertEqual(tree.freq, 1)
        self.assertRaises(AttributeError, getattr, tree, 'child')

    def test_two_symbols(self):
        freq = {"A": 1, "B": 1}
        tree = _huffman_tree(freq)
        self.assertRaises(AttributeError, getattr, tree, 'symbol')
        self.assertEqual(tree.freq, 2)
        self.assertEqual(tree.child[0].symbol, "A")
        self.assertEqual(tree.child[0].freq, 1)
        self.assertEqual(tree.child[1].symbol, "B")
        self.assertEqual(tree.child[1].freq, 1)


class DecodetreeTests(unittest.TestCase):

    def test_decodetree_large(self):
        N = 100_000
        freq = {i: random() ** 3 for i in range(N)}
        code = huffman_code(freq)
        tree = decodetree(code)
        self.assertEqual(tree.nodes(), (0, N - 1, N))


if __name__ == '__main__':
    show_info()
    unittest.main()
