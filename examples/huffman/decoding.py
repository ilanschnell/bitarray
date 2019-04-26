from __future__ import print_function
import time
from bitarray import bitarray

from huffman import (freq_string, huffTree, huffCode, write_dot,
                     make_tree, decode)


def main():
    txt = open('README').read()
    tree = huffTree(freq_string(txt))
    write_dot(tree, 'tree.dot')
    code = huffCode(tree)
    # create tree from code (no frequencies)
    write_dot(make_tree(code), 'tree_raw.dot')

    sample = 100 * txt

    a = bitarray()
    a.encode(code, sample)

    # Time the decode function above
    start_time = time.time()
    res = decode(tree, a)
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
