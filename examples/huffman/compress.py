"""
This program demonstrates how Huffman codes can be used to efficiently
compress and uncompress files (text or binary).
"""
import os
from optparse import OptionParser
from bitarray import bitarray

from huffman import (is_py3k, huffCode, huffTree, freq_string,
                     print_code, write_dot)


def is_binary(s):
    null = 0 if is_py3k else '\0'
    return bool(null in s)


def analyze(filename, printCode=False, writeDot=False):
    with open(filename, 'rb') as fi:
        s = fi.read()

    freq = freq_string(s)
    tree = huffTree(freq)
    if writeDot:
        write_dot(tree, 'tree.dot', is_binary(s))
    code = huffCode(tree)
    if printCode:
        print_code(freq, code)


def encode(filename):
    with open(filename, 'rb') as fi:
        s = fi.read()

    code = huffCode(huffTree(freq_string(s)))
    with open(filename + '.huff', 'wb') as fo:
        for c in sorted(code):
            fo.write(('%02x %s\n' % (c if is_py3k else ord(c),
                                     code[c].to01())).encode())
        a = bitarray(endian='little')
        a.encode(code, s)
        # write unused bits
        fo.write(b'unused %s\n' % str(a.buffer_info()[3]).encode())
        a.tofile(fo)
    print('%d / %d' % (len(a), 8 * len(s)))
    print('Ratio =%6.2f%%' % (100.0 * a.buffer_info()[1] / len(s)))


def decode(filename):
    assert filename.endswith('.huff')
    code = {}

    with open(filename, 'rb') as fi:
        while 1:
            line = fi.readline()
            c, b = line.split()
            if c == b'unused':
                u = int(b)
                break
            i = int(c, 16)
            code[i if is_py3k else chr(i)] = bitarray(b)
        a = bitarray(endian='little')
        a.fromfile(fi)

    if u:
        del a[-u:]

    with open(filename[:-5] + '.out', 'wb') as fo:
        for c in a.iterdecode(code):
            fo.write(chr(c).encode('ISO-8859-1') if is_py3k else c)


def main():
    p = OptionParser("usage: %prog [options] FILE")
    p.add_option(
        '-s', '--show',
        action="store_true",
        help="calculate and print the Huffman code for the "
             "frequency of characters in FILE")
    p.add_option(
        '-t', '--tree',
        action="store_true",
        help="calculate and the Huffman tree (from the frequency of "
             "characters in FILE) and write a .dot file")
    p.add_option(
        '-e', '--encode',
        action="store_true",
        help="encode (compress) FILE using the Huffman code calculated for "
             "the frequency of characters in FILE itself. "
             "The output is FILE.huff which contains both the Huffman "
             "code and the bitarray resulting from the encoding.")
    p.add_option(
        '-d', '--decode',
        action="store_true",
        help="decode (decompress) FILE.huff and write the output to FILE.out")
    p.add_option(
        '--test',
        action="store_true",
        help="encode FILE, decode FILE.huff, compare FILE with FILE.out, "
             "and unlink created files.")
    opts, args = p.parse_args()
    if len(args) != 1:
        p.error('exactly one argument required')
    filename = args[0]

    if opts.show or opts.tree:
        analyze(filename, printCode=opts.show, writeDot=opts.tree)

    if opts.encode:
        encode(filename)

    if opts.decode:
        decode(filename + '.huff')

    if opts.test:
        huff = filename + '.huff'
        out = filename + '.out'
        encode(filename)
        decode(huff)
        assert open(filename, 'rb').read() == open(out, 'rb').read()
        os.unlink(huff)
        os.unlink(out)


if __name__ == '__main__':
    main()
