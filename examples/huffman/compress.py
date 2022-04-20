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
                           vl_encode, vl_decode, huffman_code)

def encode_code(code):
    res = bytearray(struct.pack("<H", len(code)))
    for sym in sorted(code):
        res.append(sym)
        res.extend(vl_encode(code[sym]))
    return res

def decode_code(stream):
    size = struct.unpack("<H", bytes(islice(stream, 2)))[0]
    code = {}
    for _ in range(size):
        sym = next(stream)
        code[sym] = vl_decode(stream)
    return code

def create_code(cnt):
    if len(cnt) > 0:
        return huffman_code(cnt)
    # special case for empty file
    return {0: bitarray('0')}

def encode(filename):
    with open(filename, 'rb') as fi:
        plain = fi.read()

    code = create_code(Counter(plain))
    with open(filename + '.huff', 'wb') as fo:
        fo.write(encode_code(code))
        a = bitarray(endian='little')
        a.encode(code, plain)
        fo.write(serialize(a))

    if len(plain) == 0:
        assert len(a) == 0
    else:
        print('Bits: %d / %d' % (len(a), 8 * len(plain)))
        print('Ratio =%6.2f%%' % (100.0 * a.buffer_info()[1] / len(plain)))

def decode(filename):
    assert filename.endswith('.huff')

    with open(filename, 'rb') as fi:
        stream = iter(fi.read())
    code = decode_code(stream)
    a = deserialize(bytes(stream))

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

    elif opts.decode:
        decode(filename + '.huff')

    elif opts.test:
        huff = filename + '.huff'
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
