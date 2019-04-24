from __future__ import print_function
import time
from pprint import pprint
from bitarray import bitarray
from huffman import freq_string, huffCode


CNT = 1

class Node:
    def __init__(self):
        global CNT
        self.symbol = None
        self.child = [None, None]
        self.id = CNT  # used in display_tree only
        CNT += 1


def insert(tree, ba, sym):
    nd = tree
    for k in ba:
        prev = nd
        nd = nd.child[k]
        if nd and nd.symbol:
            print("ambiguity")
        if not nd:
            nd = Node()
            prev.child[k] = nd
    if nd.symbol or nd.child[0] or nd.child[1]:
        print("ambiguity")
    nd.symbol = sym


def traverse(tree, it):
    nd = tree
    while 1:
        nd = nd.child[next(it)]
        if not nd:
            print("prefix code does not match data in bitarray")
            return None
        if nd.symbol is not None:
            return nd.symbol
    if nd != tree:
        print("decoding not terminated")


def display_tree(nd):
    print("id: %3d child0: %3d child1: %3d symbol: %r" %
          (nd.id,
           nd.child[0].id if nd.child[0] else 0,
           nd.child[1].id if nd.child[1] else 0,
           nd.symbol))
    for k in range(2):
        if nd.child[k]:
            display_tree(nd.child[k])


def decode(codedict, bitsequence):
    """
    this function does the same thing as the bitarray decode method
    """
    # generate tree from codedict
    tree = Node()
    for sym, ba in codedict.items():
        insert(tree, ba, sym)
    display_tree(tree)

    # actual decoding by traversing until StopIteration
    res = []
    it = iter(bitsequence)
    while True:
        try:
            r = traverse(tree, it)
        except StopIteration:
            break
        res.append(r)
    return res


def main():
    txt = open('README').read()
    code = huffCode(freq_string(txt))
    pprint(code)

    sample = 500 * txt

    a = bitarray()
    a.encode(code, sample)

    # Time the decode function above
    start_time = time.time()
    res = decode(code, a)
    Py_time = time.time() - start_time
    assert ''.join(res) == sample
    print('Py_time: %.6f sec' % Py_time)

    # Time the decode method which is implemented in C
    start_time = time.time()
    res = a.decode(code)
    C_time = time.time() - start_time
    assert ''.join(res) == sample
    print('C_time: %.6f sec' % C_time)

    print('Ratio: %f' % (Py_time / C_time))


if __name__ == '__main__':
    main()
