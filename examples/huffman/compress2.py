"""
This program demonstrates how Huffman codes can be used to efficiently
compress and uncompress files (text or binary).
"""
import os
import struct
from itertools import islice
from optparse import OptionParser
from collections import Counter

from bitarray import bitarray
from bitarray.util import (serialize, deserialize,
                           canonical_huffman, canonical_decode)

def create_code(cnt):
    if len(cnt) > 1:
        return canonical_huffman(cnt)
    # special case for when we encode an empty file or a file with only
    # one character (possibly many of the same single character, e.g. "xxx")
    sym = list(cnt)[0] if cnt else 0
    return {sym: bitarray('0')}, [0, 1], [sym]

def encode(filename):
    with open(filename, 'rb') as fi:
        plain = fi.read()

    code, count, symbol = create_code(Counter(plain))
    with open(filename + '.huff2', 'wb') as fo:
        fo.write(struct.pack("<B", len(count)))
        for i in range(1, len(count)):
            fo.write(struct.pack("<H", count[i]))
        fo.write(struct.pack("<H", len(symbol)))
        fo.write(bytearray(symbol))

        a = bitarray(endian='little')
        a.encode(code, plain)
        fo.write(serialize(a))

    if len(plain) > 0:
        print('Bits: %d / %d' % (len(a), 8 * len(plain)))
        print('Ratio =%6.2f%%' % (100.0 * a.buffer_info()[1] / len(plain)))

def decode(filename):
    assert filename.endswith('.huff2')

    with open(filename, 'rb') as fi:
        stream = iter(fi.read())

    maxbits = struct.unpack("<B", bytes(islice(stream, 1)))[0]
    count = [0] + [struct.unpack("<H", bytes(islice(stream, 2)))[0]
                   for _ in range(maxbits - 1)]

    symbol_n = struct.unpack("<H", bytes(islice(stream, 2)))[0]
    symbol = list(bytearray(islice(stream, symbol_n)))

    a = deserialize(bytes(stream))
    with open(filename[:-6] + '.out', 'wb') as fo:
        fo.write(bytearray(canonical_decode(a, count, symbol)))

def main():
    p = OptionParser("usage: %prog [options] FILE")
    p.add_option(
        '-e', '--encode',
        action="store_true",
        help="encode (compress) FILE using the Huffman code calculated for "
             "the frequency of characters in FILE itself. "
             "The output is FILE.huff2 which contains both the Huffman "
             "code and the bitarray resulting from the encoding.")
    p.add_option(
        '-d', '--decode',
        action="store_true",
        help="decode (decompress) FILE.huff2 and write the output to FILE.out")
    p.add_option(
        '-t', '--test',
        action="store_true",
        help="encode FILE, decode FILE.huff2, compare FILE with FILE.out, "
             "and unlink created files.")
    opts, args = p.parse_args()
    if len(args) != 1:
        p.error('exactly one argument required')
    filename = args[0]

    if opts.encode:
        encode(filename)

    elif opts.decode:
        decode(filename + '.huff2')

    elif opts.test:
        huff = filename + '.huff2'
        out = filename + '.out'
        encode(filename)
        decode(huff)
        assert open(filename, 'rb').read() == open(out, 'rb').read()
        os.unlink(huff)
        os.unlink(out)

    else:
        p.error("no option provided")


if __name__ == '__main__':
    main()
