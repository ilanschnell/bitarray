import unittest
from random import random

from bitarray.util import _huffman_tree, huffman_code


class HuffmanTree_Tests(unittest.TestCase):

    # tests for util._huffman_tree()

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

    def test_code_matches_tree(self):
        N = 567
        freq = {i: random() ** 3 for i in range(N)}
        tree = _huffman_tree(freq)
        code = huffman_code(freq)
        self.assertEqual(set(code), set(freq))
        for sym, a in code.items():
            nd = tree
            for k in a:
                self.assertRaises(AttributeError, getattr, nd, 'symbol')
                nd = nd.child[k]
            self.assertEqual(sym, nd.symbol)
            self.assertRaises(AttributeError, getattr, nd, 'child')


if __name__ == '__main__':
    unittest.main()
