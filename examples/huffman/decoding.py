from optparse import OptionParser
from time import perf_counter
from collections import Counter

from bitarray import bitarray
from bitarray.util import _huffman_tree

from huffman import (huff_code, write_dot, print_code,
                     make_tree, iterdecode)


def main():
    p = OptionParser("usage: %prog [options] [FILE]")
    p.add_option(
        '-p', '--print',
        action="store_true",
        help="print Huffman code")
    p.add_option(
        '-t', '--tree',
        action="store_true",
        help="store the tree as a .dot file")
    opts, args = p.parse_args()

    if len(args) == 0:
        filename = 'README'
    elif len(args) == 1:
        filename = args[0]
    else:
        p.error('only one argument expected')

    with open(filename, 'rb') as fi:
        plain = bytearray(fi.read())

    t0 = perf_counter()
    freq = Counter(plain)
    print('count:     %9.3f ms' % (1000.0 * (perf_counter() - t0)))

    t0 = perf_counter()
    tree = _huffman_tree(freq)
    print('tree:      %9.3f ms' % (1000.0 * (perf_counter() - t0)))

    if opts.tree:
        write_dot(tree, 'tree.dot', 0 in plain)
    code = huff_code(tree)
    if opts.print:
        print_code(freq, code)
    if opts.tree:
        # create tree from code (no frequencies)
        write_dot(make_tree(code), 'tree_raw.dot', 0 in plain)

    a = bitarray()

    t0 = perf_counter()
    a.encode(code, plain)
    print('C encode:  %9.3f ms' % (1000.0 * (perf_counter() - t0)))

    # Time the decode function above
    t0 = perf_counter()
    res = bytearray(iterdecode(tree, a))
    Py_time = perf_counter() - t0
    print('Py decode: %9.3f ms' % (1000.0 * Py_time))
    assert res == plain

    # Time the decode method which is implemented in C
    t0 = perf_counter()
    res = bytearray(a.iterdecode(code))
    C_time = perf_counter() - t0
    print('C decode:  %9.3f ms' % (1000.0 * C_time))
    assert res == plain

    print('Ratio: %f' % (Py_time / C_time))


if __name__ == '__main__':
    main()
