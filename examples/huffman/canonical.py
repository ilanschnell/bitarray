from collections import Counter

from bitarray import bitarray
from bitarray.util import canonical_huffman, canonical_decode

from huffman import write_dot, print_code, make_tree


def main():
    from optparse import OptionParser

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

    freq = Counter(plain)
    code, count, symbol = canonical_huffman(freq)
    if opts.print:
        print_code(freq, code)
    if opts.tree:
        # create tree from code (no frequencies)
        write_dot(make_tree(code), 'tree_raw.dot', 0 in plain)

    a = bitarray()
    a.encode(code, plain)
    assert bytearray(a.iterdecode(code)) == plain
    assert bytearray(canonical_decode(a, count, symbol)) == plain


if __name__ == '__main__':
    main()
