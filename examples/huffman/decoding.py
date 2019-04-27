from __future__ import print_function
from time import time
from collections import Counter
from bitarray import bitarray

from huffman import huffTree, huffCode, write_dot, make_tree, decode


def main():
    txt = 1000 * open('README').read()

    t0 = time()
    freq = Counter(txt)
    print('count:     %9.6f sec' % (time() - t0))

    t0 = time()
    tree = huffTree(freq)
    print('tree:      %9.6f sec' % (time() - t0))

    write_dot(tree, 'tree.dot')
    code = huffCode(tree)
    # create tree from code (no frequencies)
    write_dot(make_tree(code), 'tree_raw.dot')

    a = bitarray()

    t0 = time()
    a.encode(code, txt)
    print('C encode:  %9.6f sec' % (time() - t0))

    # Time the decode function above
    t0 = time()
    res = decode(tree, a)
    Py_time = time() - t0
    assert ''.join(res) == txt
    print('Py decode: %9.6f sec' % Py_time)

    # Time the decode method which is implemented in C
    t0 = time()
    res = a.decode(code)
    assert ''.join(res) == txt
    C_time = time() - t0
    print('C decode:  %9.6f sec' % C_time)

    print('Ratio: %f' % (Py_time / C_time))


if __name__ == '__main__':
    main()
