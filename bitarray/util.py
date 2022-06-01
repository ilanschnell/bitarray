# Copyright (c) 2019 - 2022, Ilan Schnell; All Rights Reserved
# bitarray is published under the PSF license.
#
# Author: Ilan Schnell
"""
Useful utilities for working with bitarrays.
"""
from __future__ import absolute_import

import os
import sys

from bitarray import bitarray, bits2bytes

from bitarray._util import (
    count_n, rindex, parity, count_and, count_or, count_xor, subset,
    serialize, ba2hex, _hex2ba, ba2base, _base2ba, vl_encode, _vl_decode,
    canonical_decode,
)

__all__ = [
    'zeros', 'urandom', 'pprint', 'make_endian', 'rindex', 'strip', 'count_n',
    'parity', 'count_and', 'count_or', 'count_xor', 'subset',
    'ba2hex', 'hex2ba', 'ba2base', 'base2ba', 'ba2int', 'int2ba',
    'serialize', 'deserialize', 'vl_encode', 'vl_decode',
    'huffman_code', 'canonical_huffman', 'canonical_decode',
]


_is_py2 = bool(sys.version_info[0] == 2)


def zeros(__length, endian=None):
    """zeros(length, /, endian=None) -> bitarray

Create a bitarray of length, with all values 0, and optional
endianness, which may be 'big', 'little'.
"""
    if not isinstance(__length, (int, long) if _is_py2 else int):
        raise TypeError("int expected, got '%s'" % type(__length).__name__)

    a = bitarray(__length, endian)
    a.setall(0)
    return a


def urandom(__length, endian=None):
    """urandom(length, /, endian=None) -> bitarray

Return a bitarray of `length` random bits (uses `os.urandom`).
"""
    a = bitarray(0, endian)
    a.frombytes(os.urandom(bits2bytes(__length)))
    del a[__length:]
    return a


def pprint(__a, stream=None, group=8, indent=4, width=80):
    """pprint(bitarray, /, stream=None, group=8, indent=4, width=80)

Prints the formatted representation of object on `stream` (which defaults
to `sys.stdout`).  By default, elements are grouped in bytes (8 elements),
and 8 bytes (64 elements) per line.
Non-bitarray objects are printed by the standard library
function `pprint.pprint()`.
"""
    if stream is None:
        stream = sys.stdout

    if not isinstance(__a, bitarray):
        import pprint as _pprint
        _pprint.pprint(__a, stream=stream, indent=indent, width=width)
        return

    group = int(group)
    if group < 1:
        raise ValueError('group must be >= 1')
    indent = int(indent)
    if indent < 0:
        raise ValueError('indent must be >= 0')
    width = int(width)
    if width <= indent:
        raise ValueError('width must be > %d (indent)' % indent)

    gpl = (width - indent) // (group + 1)  # groups per line
    epl = group * gpl                      # elements per line
    if epl == 0:
        epl = width - indent - 2
    type_name = type(__a).__name__
    # here 4 is len("'()'")
    multiline = len(type_name) + 4 + len(__a) + len(__a) // group >= width
    if multiline:
        quotes = "'''"
    elif __a:
        quotes = "'"
    else:
        quotes = ""

    stream.write("%s(%s" % (type_name, quotes))
    for i, b in enumerate(__a):
        if multiline and i % epl == 0:
            stream.write('\n%s' % (indent * ' '))
        if i % group == 0 and i % epl != 0:
            stream.write(' ')
        stream.write(str(b))

    if multiline:
        stream.write('\n')

    stream.write("%s)\n" % quotes)
    stream.flush()


def make_endian(__a, endian):
    """make_endian(bitarray, /, endian) -> bitarray

When the endianness of the given bitarray is different from `endian`,
return a new bitarray, with endianness `endian` and the same elements
as the original bitarray.
Otherwise (endianness is already `endian`) the original bitarray is returned
unchanged.
"""
    if not isinstance(__a, bitarray):
        raise TypeError("bitarray expected, got '%s'" % type(__a).__name__)

    if __a.endian() == endian:
        return __a

    return bitarray(__a, endian)


def strip(__a, mode='right'):
    """strip(bitarray, /, mode='right') -> bitarray

Return a new bitarray with zeros stripped from left, right or both ends.
Allowed values for mode are the strings: `left`, `right`, `both`
"""
    if not isinstance(__a, bitarray):
        raise TypeError("bitarray expected, got '%s'" % type(__a).__name__)
    if not isinstance(mode, str):
        raise TypeError("str expected for mode, got '%s'" % type(__a).__name__)
    if mode not in ('left', 'right', 'both'):
        raise ValueError("mode must be 'left', 'right' or 'both', got %r" %
                         mode)
    first = 0
    if mode in ('left', 'both'):
        try:
            first = __a.index(1)
        except ValueError:
            return __a[:0]

    last = len(__a) - 1
    if mode in ('right', 'both'):
        try:
            last = rindex(__a)
        except ValueError:
            return __a[:0]

    return __a[first:last + 1]


def hex2ba(__s, endian=None):
    """hex2ba(hexstr, /, endian=None) -> bitarray

Bitarray of hexadecimal representation.  hexstr may contain any number
(including odd numbers) of hex digits (upper or lower case).
"""
    if isinstance(__s, unicode if _is_py2 else str):
        __s = __s.encode('ascii')
    if not isinstance(__s, bytes):
        raise TypeError("str expected, got '%s'" % type(__s).__name__)

    a = bitarray(4 * len(__s), endian)
    _hex2ba(a, __s)
    return a


def base2ba(__n, __s, endian=None):
    """base2ba(n, asciistr, /, endian=None) -> bitarray

Bitarray of the base `n` ASCII representation.
Allowed values for `n` are 2, 4, 8, 16, 32 and 64.
For `n=16` (hexadecimal), `hex2ba()` will be much faster, as `base2ba()`
does not take advantage of byte level operations.
For `n=32` the RFC 4648 Base32 alphabet is used, and for `n=64` the
standard base 64 alphabet is used.
"""
    if isinstance(__s, unicode if _is_py2 else str):
        __s = __s.encode('ascii')
    if not isinstance(__s, bytes):
        raise TypeError("str expected, got '%s'" % type(__s).__name__)

    a = bitarray(_base2ba(__n) * len(__s), endian)
    _base2ba(__n, a, __s)
    return a


def ba2int(__a, signed=False):
    """ba2int(bitarray, /, signed=False) -> int

Convert the given bitarray to an integer.
The bit-endianness of the bitarray is respected.
`signed` indicates whether two's complement is used to represent the integer.
"""
    if not isinstance(__a, bitarray):
        raise TypeError("bitarray expected, got '%s'" % type(__a).__name__)
    length = len(__a)
    if length == 0:
        raise ValueError("non-empty bitarray expected")

    le = bool(__a.endian() == 'little')
    if __a.padbits:
        pad = zeros(__a.padbits, __a.endian())
        __a = __a + pad if le else pad + __a

    if _is_py2:
        a = bitarray(__a, 'big')
        if le:
            a.reverse()
        res = int(ba2hex(a), 16)
    else: # py3
        res = int.from_bytes(__a.tobytes(), byteorder=__a.endian())

    if signed and res >= 1 << (length - 1):
        res -= 1 << length
    return res


def int2ba(__i, length=None, endian=None, signed=False):
    """int2ba(int, /, length=None, endian=None, signed=False) -> bitarray

Convert the given integer to a bitarray (with given endianness,
and no leading (big-endian) / trailing (little-endian) zeros), unless
the `length` of the bitarray is provided.  An `OverflowError` is raised
if the integer is not representable with the given number of bits.
`signed` determines whether two's complement is used to represent the integer,
and requires `length` to be provided.
"""
    if not isinstance(__i, (int, long) if _is_py2 else int):
        raise TypeError("int expected, got '%s'" % type(__i).__name__)
    if length is not None:
        if not isinstance(length, int):
            raise TypeError("int expected for length")
        if length <= 0:
            raise ValueError("length must be > 0")
    if signed and length is None:
        raise TypeError("signed requires length")

    if __i == 0:
        # there are special cases for 0 which we'd rather not deal with below
        return zeros(length or 1, endian)

    if signed:
        m = 1 << (length - 1)
        if not (-m <= __i < m):
            raise OverflowError("signed integer not in range(%d, %d), "
                                "got %d" % (-m, m, __i))
        if __i < 0:
            __i += 1 << length
    else:  # unsigned
        if __i < 0:
            raise OverflowError("unsigned integer not positive, got %d" % __i)
        if length and __i >= (1 << length):
            raise OverflowError("unsigned integer not in range(0, %d), "
                                "got %d" % (1 << length, __i))

    a = bitarray(0, endian)
    le = bool(a.endian() == 'little')
    if _is_py2:
        s = hex(__i)[2:].rstrip('L')
        a.extend(hex2ba(s, 'big'))
        if le:
            a.reverse()
    else: # py3
        b = __i.to_bytes(bits2bytes(__i.bit_length()), byteorder=a.endian())
        a.frombytes(b)

    if length is None:
        return strip(a, 'right' if le else 'left')

    la = len(a)
    if la > length:
        a = a[:length] if le else a[-length:]
    if la < length:
        pad = zeros(length - la, endian)
        a = a + pad if le else pad + a
    assert len(a) == length
    return a


def deserialize(__b):
    """deserialize(bytes, /) -> bitarray

Return a bitarray given a bytes-like representation such as returned
by `serialize()`.
"""
    if isinstance(__b, int):  # as bytes(n) will return n NUL bytes
        raise TypeError("cannot convert 'int' object to bytes")
    if not isinstance(__b, bytes):
        __b = bytes(__b)
    if len(__b) == 0:
        raise ValueError("non-empty bytes expected")

    if _is_py2:
        head = ord(__b[0])
        if head >= 32 or head % 16 >= 8:
            raise ValueError('invalid header byte: 0x%02x' % head)
    try:
        return bitarray(__b)
    except TypeError:
        raise ValueError('invalid header byte: 0x%02x' % __b[0])


def vl_decode(__stream, endian=None):
    """vl_decode(stream, /, endian=None) -> bitarray

Decode binary stream (an integer iterator, or bytes-like object), and return
the decoded bitarray.  This function consumes only one bitarray and leaves
the remaining stream untouched.  `StopIteration` is raised when no
terminating byte is found.
Use `vl_encode()` for encoding.
"""
    a = bitarray(32, endian)
    _vl_decode(iter(__stream), a)
    return a

# ------------------------------ Huffman coding -----------------------------

def _huffman_tree(__freq_map):
    """_huffman_tree(dict, /) -> Node

Given a dict mapping symbols to their frequency, construct a Huffman tree
and return its root node.
"""
    from heapq import heappush, heappop

    class Node(object):
        """
        A Node instance will either have a 'symbol' (leaf node) or
        a 'child' (a tuple with both children) attribute.
        The 'freq' attribute will always be present.
        """
        def __lt__(self, other):
            # heapq needs to be able to compare the nodes
            return self.freq < other.freq

    minheap = []
    # create all leaf nodes and push them onto the queue
    for sym, f in __freq_map.items():
        leaf = Node()
        leaf.symbol = sym
        leaf.freq = f
        heappush(minheap, leaf)

    # repeat the process until only one node remains
    while len(minheap) > 1:
        # take the two nodes with lowest frequencies from the queue
        # to construct a new node and push it onto the queue
        parent = Node()
        parent.child = heappop(minheap), heappop(minheap)
        parent.freq = parent.child[0].freq + parent.child[1].freq
        heappush(minheap, parent)

    # the single remaining node is the root of the Huffman tree
    return minheap[0]


def huffman_code(__freq_map, endian=None):
    """huffman_code(dict, /, endian=None) -> dict

Given a frequency map, a dictionary mapping symbols to their frequency,
calculate the Huffman code, i.e. a dict mapping those symbols to
bitarrays (with given endianness).  Note that the symbols are not limited
to being strings.  Symbols may may be any hashable object (such as `None`).
"""
    if not isinstance(__freq_map, dict):
        raise TypeError("dict expected, got '%s'" % type(__freq_map).__name__)

    b0 = bitarray('0', endian)
    b1 = bitarray('1', endian)

    if len(__freq_map) < 2:
        if len(__freq_map) == 0:
            raise ValueError("cannot create Huffman code with no symbols")
        # Only one symbol: Normally if only one symbol is given, the code
        # could be represented with zero bits.  However here, the code should
        # be at least one bit for the .encode() and .decode() methods to work.
        # So we represent the symbol by a single code of length one, in
        # particular one 0 bit.  This is an incomplete code, since if a 1 bit
        # is received, it has no meaning and will result in an error.
        return {list(__freq_map)[0]: b0}

    result = {}

    def traverse(nd, prefix=bitarray(0, endian)):
        try:                    # leaf
            result[nd.symbol] = prefix
        except AttributeError:  # parent, so traverse each of the children
            traverse(nd.child[0], prefix + b0)
            traverse(nd.child[1], prefix + b1)

    traverse(_huffman_tree(__freq_map))
    return result


def canonical_huffman(__freq_map):
    """canonical_huffman(dict, /) -> tuple

Given a frequency map, a dictionary mapping symbols to their frequency,
calculate the canonical Huffman code.  Returns a tuple containing:

0. the canonical Huffman code as a dict mapping symbols to bitarrays
1. a list containing the number of symbols of each code length
2. a list of symbols in canonical order

Note: the two lists may be used as input for `canonical_decode()`.
"""
    if not isinstance(__freq_map, dict):
        raise TypeError("dict expected, got '%s'" % type(__freq_map).__name__)

    if len(__freq_map) < 2:
        if len(__freq_map) == 0:
            raise ValueError("cannot create Huffman code with no symbols")
        # Only one symbol: see note above in huffman_code()
        sym = list(__freq_map)[0]
        return {sym: bitarray('0', 'big')}, [0, 1], [sym]

    code_length = {}  # map symbols to their code length

    def traverse(nd, length=0):
        # traverse the Huffman tree, but (unlike in huffman_code() above) we
        # now just simply record the length for reaching each symbol
        try:                    # leaf
            code_length[nd.symbol] = length
        except AttributeError:  # parent, so traverse each of the children
            traverse(nd.child[0], length + 1)
            traverse(nd.child[1], length + 1)

    traverse(_huffman_tree(__freq_map))

    # we now have a mapping of symbols to their code length,
    # which is all we need

    table = sorted(code_length.items(), key=lambda item: (item[1], item[0]))

    maxbits = max(item[1] for item in table)
    codedict = {}
    count = (maxbits + 1) * [0]

    code = 0
    for i, (sym, length) in enumerate(table):
        codedict[sym] = int2ba(code, length, 'big')
        count[length] += 1
        if i + 1 < len(table):
            code += 1
            code <<= table[i + 1][1] - length

    return codedict, count, [item[0] for item in table]
