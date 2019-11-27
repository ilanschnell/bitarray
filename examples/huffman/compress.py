"""
This program demonstrates how Huffman codes can be used to efficiently
compress and uncompress files (text or binary).
"""
import os
from optparse import OptionParser
from collections import Counter
from bitarray import bitarray
from bitarray.util import huffman_code


def encode(filename):
    with open(filename, 'rb') as fi:
        plain = bytearray(fi.read())

    code = huffman_code(Counter(plain))
    with open(filename + '.huff', 'wb') as fo:
        for sym in sorted(code):
            fo.write(('%02x %s\n' % (sym, code[sym].to01())).encode())
        a = bitarray(endian='little')
        a.encode(code, plain)
        # write unused bits
        fo.write(b'unused %s\n' % str(a.buffer_info()[3]).encode())
        a.tofile(fo)
    print('Bits: %d / %d' % (len(a), 8 * len(plain)))
    print('Ratio =%6.2f%%' % (100.0 * a.buffer_info()[1] / len(plain)))


def decode(filename):
    assert filename.endswith('.huff')
    code = {}

    with open(filename, 'rb') as fi:
        while 1:
            line = fi.readline()
            sym, b = line.split()
            if sym == b'unused':
                u = int(b)
                break
            i = int(sym, 16)
            code[i] = bitarray(b)
        a = bitarray(endian='little')
        a.fromfile(fi)

    if u:
        del a[-u:]

    with open(filename[:-5] + '.out', 'wb') as fo:
        fo.write(bytearray(a.iterdecode(code)))


def main():
    p = OptionParser("usage: %prog [options] FILE")
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
        '-t', '--test',
        action="store_true",
        help="encode FILE, decode FILE.huff, compare FILE with FILE.out, "
             "and unlink created files.")
    opts, args = p.parse_args()
    if len(args) != 1:
        p.error('exactly one argument required')
    filename = args[0]

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
