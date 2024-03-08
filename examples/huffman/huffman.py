"""
This library contains useful functionality for working with Huffman trees
and codes.

Note:
There is a function for directly creating a Huffman code from a frequency
map in the bitarray library itself: bitarray.util.huffman_code()
"""
from bitarray import bitarray


class Node:
    def __init__(self):
        self.child = [None, None]
        self.freq = None

    def __lt__(self, other):
        # heapq needs to be able to compare the nodes
        return self.freq < other.freq


def huff_code(tree):
    """
    Given a Huffman tree, traverse the tree and return the Huffman code, i.e.
    a dictionary mapping symbols to bitarrays.
    """
    result = {}

    def traverse(nd, prefix=bitarray()):
        try:  # leaf
            result[nd.symbol] = prefix
        except AttributeError:
            traverse(nd.child[0], prefix + bitarray([0]))
            traverse(nd.child[1], prefix + bitarray([1]))

    traverse(tree)
    return result


def insert_symbol(tree, ba, sym):
    """
    Insert symbol into a tree at the position described by the bitarray,
    creating nodes as necessary.
    """
    nd = tree
    for k in ba:
        prev = nd
        nd = nd.child[k]

        if hasattr(nd, 'symbol'):
            raise ValueError("ambiguity")

        if nd is None:
            nd = Node()
            prev.child[k] = nd

    if hasattr(nd, 'symbol') or nd.child[0] or nd.child[1]:
        raise ValueError("ambiguity")

    nd.symbol = sym


def make_tree(codedict):
    """
    Create a tree from the given code dictionary, and return its root node.
    Unlike trees created by huff_tree, all nodes will have .freq set to None.
    """
    tree = Node()
    for sym, ba in codedict.items():
        insert_symbol(tree, ba, sym)
    return tree


def traverse(tree, it):
    """
    Traverse tree until a leaf node is reached, and return its symbol.
    This function consumes an iterator on which next() is called during each
    step of traversing.
    """
    nd = tree
    while 1:
        nd = nd.child[next(it)]
        if nd is None:
            raise ValueError("prefix code does not match data in bitarray")

        try:
            return nd.symbol
        except AttributeError:
            pass

    if nd != tree:
        raise ValueError("decoding not terminated")


def iterdecode(tree, bitsequence):
    """
    Given a tree and a bitsequence, decode the bitsequence and generate
    the symbols.
    """
    it = iter(bitsequence)
    while True:
        try:
            yield traverse(tree, it)
        except StopIteration:
            return


def write_dot(tree, fn, binary=False):
    """
    Given a tree (which may or may not contain frequencies), write
    a graphviz '.dot' file with a visual representation of the tree.
    """
    special_ascii = {' ': 'SPACE', '\n': 'LF', '\r': 'CR', '\t': 'TAB',
                     '\\': r'\\', '"': r'\"'}
    def disp_sym(i):
        if binary:
            return '0x%02x' % i
        else:
            c = chr(i)
            res = special_ascii.get(c, c)
            assert res.strip(), repr(c)
            return res

    def disp_freq(f):
        if f is None:
            return ''
        return '%d' % f

    def write_nd(fo, nd):
        if hasattr(nd, 'symbol'):  # leaf node
            a, b = disp_freq(nd.freq), disp_sym(nd.symbol)
            fo.write('  %d  [label="%s%s%s"];\n' %
                     (id(nd), a, ': ' if a and b else '', b))
            return

        assert hasattr(nd, 'child')
        fo.write('  %d  [shape=circle, style=filled, '
                 'fillcolor=grey, label="%s"];\n' %
                 (id(nd), disp_freq(nd.freq)))

        for k in range(2):
            if nd.child[k]:
                fo.write('  %d->%d;\n' % (id(nd), id(nd.child[k])))

        for k in range(2):
            if nd.child[k]:
                write_nd(fo, nd.child[k])

    with open(fn, 'w') as fo:    # dot -Tpng tree.dot -O
        fo.write('digraph BT {\n')
        fo.write('  node [shape=box, fontsize=20, fontname="Arial"];\n')
        write_nd(fo, tree)
        fo.write('}\n')


def print_code(freq, codedict):
    """
    Given a frequency map (dictionary mapping symbols to their frequency)
    and a codedict, print them in a readable form.
    """
    special_ascii = {0: 'NUL', 9: 'TAB', 10: 'LF', 13: 'CR', 127: 'DEL'}
    def disp_char(i):
        if 32 <= i < 127:
            return repr(chr(i))
        return special_ascii.get(i, '')

    print(' symbol     char    hex   frequency     Huffman code')
    print(70 * '-')
    for i in sorted(codedict, key=lambda c: (freq[c], c), reverse=True):
        print('%7r     %-4s    0x%02x %10i     %s' % (
            i, disp_char(i), i, freq[i], codedict[i].to01()))


def test():
    from bitarray.util import _huffman_tree

    freq = {'a': 10, 'b': 2, 'c': 1}
    tree = _huffman_tree(freq)
    code = huff_code(tree)
    assert len(code['a']) == 1
    assert len(code['b']) == len(code['c']) == 2

    code = {'a': bitarray('0'),
            'b': bitarray('10'),
            'c': bitarray('11')}
    tree = make_tree(code)
    txt = 'abca'
    a = bitarray()
    a.encode(code, txt)
    assert a == bitarray('010110')
    assert ''.join(iterdecode(tree, a)) == txt


if __name__ == '__main__':
    test()
