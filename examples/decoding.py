import time
from bitarray import bitarray
from huffman import freq_string, huffCode


def insert(nd, ba, sym):
    for k in ba:
        prev = nd
        nd = nd[k]
        if not nd:
            nd = [[], []]
            prev[k] = nd
    nd[0] = sym
    del nd[1]


def traverse(nd, it):
    while len(nd) == 2:
        nd = nd[next(it)]
    return nd[0]


def decode(codedict, bitsequence):
    """
    this function does the same thing as the bitarray decode method
    """
    # generate tree from codedict
    root = [[], []]
    for sym, ba in codedict.items():
        insert(root, ba, sym)

    # actual decoding by traversing until StopIteration
    res = []
    it = iter(bitsequence)
    while True:
        try:
            r = traverse(root, it)
        except StopIteration:
            break
        res.append(r)
    return res


def main():
    txt = open('README').read()
    code = huffCode(freq_string(txt))

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
