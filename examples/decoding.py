from __future__ import print_function
import time
from pprint import pprint
from bitarray import bitarray
from huffman import freq_string, huffCode



class Node:
    def __init__(self):
        self.child = [None, None]
        self.symbol = None


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


def write_dot(tree):

    special_ascii = {' ': 'SPACE', '\n': 'LF', '\t': 'TAB', '"': r'\"'}
    def disp_char(c):
        res = special_ascii.get(c, c)
        assert res.strip(), repr(c)
        return res

    with open('tree.dot', 'w') as fo:    # dot -Tpng tree.dot -O
        def write_nd(fo, nd):
            if nd.symbol:
                fo.write('  %d  [label="%s"];\n' % (id(nd),
                                                    disp_char(nd.symbol)))
            else:
                fo.write('  %d  [shape=circle, style=filled, '
                         'fillcolor=grey, label=""];\n' % (id(nd),))

            if nd.child[0] and nd.child[1]:
                for k in range(2):
                    fo.write('  %d->%d;\n' % (id(nd), id(nd.child[k])))

            for k in range(2):
                if nd.child[k]:
                    write_nd(fo, nd.child[k])

        fo.write('digraph BT {\n')
        fo.write('node [shape=box, fontsize=20, fontname="Arial"];\n')
        write_nd(fo, tree)
        fo.write('}\n')


def decode(codedict, bitsequence):
    """
    this function does the same thing as the bitarray decode method
    """
    # generate tree from codedict
    tree = Node()
    for sym, ba in codedict.items():
        insert(tree, ba, sym)
    write_dot(tree)

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
    #pprint(code)

    sample = 100 * txt

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
