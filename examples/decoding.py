import time
from bitarray import bitarray
from huffman import freq_string, huffCode


def traverse(it, tree):
    """
    return False, when it has no more elements, or the leave node
    resulting from traversing the tree
    """
    try:
        subtree = tree[next(it)]
    except StopIteration:
        return False

    if isinstance(subtree, list) and len(subtree)==2:
        return traverse(it, subtree)
    else: # leave node
        return subtree


def insert(tree, sym, ba):
    """
    insert symbol which is mapped to bitarray into tree
    """
    v = ba[0]
    if len(ba) > 1:
        if tree[v] == []:
            tree[v] = [[], []]
        insert(tree[v], sym, ba[1:])
    else:
        if tree[v] != []:
            raise ValueError("prefix code ambiguous")
        tree[v] = sym


def decode(codedict, bitsequence):
    """
    this function does the same thing as the bitarray decode method
    """
    # generate tree from codedict
    tree = [[], []]
    for sym, ba in codedict.items():
        insert(tree, sym, ba)

    # actual decoding by traversing until StopIteration
    res = []
    it = iter(bitsequence)
    while True:
        r = traverse(it, tree)
        if r is False:
            break
        else:
            if r == []:
                raise ValueError("prefix code does not match data")
            res.append(r)
    return res


def main():
    txt = open('README').read()
    code = huffCode(freq_string(txt))

    sample = 2000 * txt

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
