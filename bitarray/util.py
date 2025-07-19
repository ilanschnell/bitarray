# Copyright (c) 2019 - 2025, Ilan Schnell; All Rights Reserved
# bitarray is published under the PSF license.
#
# Author: Ilan Schnell
"""
Useful utilities for working with bitarrays.
"""
from __future__ import absolute_import

import os
import sys
import random

from bitarray import bitarray, bits2bytes

from bitarray._util import (
    zeros, ones, count_n, parity, xor_indices,
    count_and, count_or, count_xor, any_and, subset,
    correspond_all, byteswap,
    serialize, deserialize,
    ba2hex, hex2ba,
    ba2base, base2ba,
    sc_encode, sc_decode,
    vl_encode, vl_decode,
    canonical_decode,
)

__all__ = [
    'zeros', 'ones', 'urandom', 'random_p',
    'pprint', 'strip', 'count_n',
    'parity', 'xor_indices',
    'count_and', 'count_or', 'count_xor', 'any_and', 'subset',
    'correspond_all', 'byteswap', 'intervals',
    'ba2hex', 'hex2ba',
    'ba2base', 'base2ba',
    'ba2int', 'int2ba',
    'serialize', 'deserialize',
    'sc_encode', 'sc_decode',
    'vl_encode', 'vl_decode',
    'huffman_code', 'canonical_huffman', 'canonical_decode',
]


def urandom(__length, endian=None):
    """urandom(n, /, endian=None) -> bitarray

Return random bitarray of length `n` (uses `os.urandom()`).
"""
    a = bitarray(os.urandom(bits2bytes(__length)), endian)
    del a[__length:]
    return a


def random_p(__n, p=0.5, endian=None):
    """random_p(n, /, p=0.5, endian=None) -> bitarray

Return (pseudo-) random bitarray of length `n`.  Each bit has probability `p`
of being one (independent of any other bits).  Mathematically equivalent
to `bitarray((random() < p for _ in range(n)), endian)`, but much faster
for large `n`.  The random bitarrays are reproducible when giving
Python's `random.seed()` with a specific seed value.

This function requires Python 3.12 or higher, as it depends on the standard
library function `random.binomialvariate()`.  Raises `NotImplementedError`
when Python version is too low.
"""
    if sys.version_info[:2] < (3, 12):
        raise NotImplementedError("bitarray.util.random_p() requires "
                                  "Python 3.12 or higher")
    r = _RandomP(__n, endian)
    return r.random_p(p)

class _RandomP:

    # The main reason for this class it to enable testing functionality
    # individually in the test class Random_P_Tests in 'test_util.py'.
    # The test class also contains many comments and explanations.
    # To better understand how the algorithm works, see ./doc/random_p.rst
    # See also, VerificationTests in ./examples/test_random.py

    # maximal number of calls to .random_half() in .combine()
    M = 8

    # number of resulting probability intervals
    K = 1 << M

    # limit for setting individual bits randomly
    SMALL_P = 0.01

    def __init__(self, n=0, endian=None):
        self.n = n
        self.nbytes = bits2bytes(n)
        self.endian = endian

    def random_half(self):
        """
        Return bitarray with each bit having probability p = 1/2 of being 1.
        """
        # use randbytes() for reproducibility (not urandom())
        a = bitarray(random.randbytes(self.nbytes), self.endian)
        del a[self.n:]
        return a

    def op_seq(self, i):
        """
        Return bitarray containing operator sequence.
        Each item represents a bitwise operation:   0: AND   1: OR
        After applying the sequence (see .random_combine()), we
        obtain a bitarray with probability  q = i / K
        """
        if not 0 < i < self.K:
            raise ValueError("0 < i < %d, got i = %d" % (self.K, i))

        # sequence of &, | operations - least significant operations first
        a = bitarray(i.to_bytes(2, byteorder="little"), "little")
        return a[a.index(1) + 1 : self.M]

    def combine_half(self, seq):
        """
        Combine random bitarrays with probability 1/2
        according to given operator sequence.
        """
        a = self.random_half()
        for k in seq:
            if k:
                a |= self.random_half()
            else:
                a &= self.random_half()
        return a

    def random_pop(self, k):
        """
        Return a random bitarray of length self.n and population count k.
        Designed for small k (compared to self.n).
        """
        randrange = random.randrange
        n = self.n

        if not 0 <= k <= n:
            raise ValueError("0 <= k <= %d, got k = %d" % (n, k))

        a = bitarray(n, self.endian)
        for _ in range(k):
            i = randrange(n)
            while a[i]:
                i = randrange(n)
            a[i] = 1
        return a

    def random_p(self, p):
        # error check inputs and handle edge cases
        if p <= 0.0 or p == 0.5 or p >= 1.0:
            if p == 0.0:
                return zeros(self.n, self.endian)
            if p == 0.5:
                return self.random_half()
            if p == 1.0:
                return ones(self.n, self.endian)
            raise ValueError("p must be in range 0.0 <= p <= 1.0, got %s" % p)

        # for small n, use literal definition
        if self.n < 16:
            return bitarray((random.random() < p for _ in range(self.n)),
                            self.endian)

        # exploit symmetry to establish: p < 0.5
        if p > 0.5:
            a = self.random_p(1.0 - p)
            a.invert()  # use in-place to avoid copying
            return a

        # for small p, set randomly individual bits
        if p < self.SMALL_P:
            return self.random_pop(random.binomialvariate(self.n, p))

        # calculate operator sequence
        i = int(p * self.K)
        if p * (self.K + 1) > i + 1: # see ./examples/test_random.py
            i += 1
        seq = self.op_seq(i)
        q = i / self.K

        # when n is small compared to number of operations, also use literal
        if self.n < 100 and self.nbytes <= len(seq) + 3 * bool(q != p):
            return bitarray((random.random() < p for _ in range(self.n)),
                            self.endian)

        # combine random bitarrays using bitwise AND and OR operations
        a = self.combine_half(seq)
        if q < p:
            x = (p - q) / (1.0 - q)
            a |= self.random_p(x)
        elif q > p:
            x = p / q
            a &= self.random_p(x)

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


def strip(__a, mode='right'):
    """strip(bitarray, /, mode='right') -> bitarray

Return a new bitarray with zeros stripped from left, right or both ends.
Allowed values for mode are the strings: `left`, `right`, `both`
"""
    if not isinstance(mode, str):
        raise TypeError("str expected for mode, got '%s'" %
                        type(__a).__name__)
    if mode not in ('left', 'right', 'both'):
        raise ValueError("mode must be 'left', 'right' or 'both', got %r" %
                         mode)

    start = None if mode == 'right' else __a.find(1)
    if start == -1:
        return __a[:0]
    stop = None if mode == 'left' else __a.find(1, right=1) + 1
    return __a[start:stop]


def intervals(__a):
    """intervals(bitarray, /) -> iterator

Compute all uninterrupted intervals of 1s and 0s, and return an
iterator over tuples `(value, start, stop)`.  The intervals are guaranteed
to be in order, and their size is always non-zero (`stop - start > 0`).
"""
    try:
        value = __a[0]  # value of current interval
    except IndexError:
        return
    n = len(__a)
    stop = 0  # "previous" stop - becomes next start

    while stop < n:
        start = stop
        # assert __a[start] == value
        try:  # find next occurrence of opposite value
            stop = __a.index(not value, start)
        except ValueError:
            stop = n
        yield int(value), start, stop
        value = not value  # next interval has opposite value


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

    if __a.padbits:
        pad = zeros(__a.padbits, __a.endian)
        __a = __a + pad if __a.endian == "little" else pad + __a

    res = int.from_bytes(__a.tobytes(), byteorder=__a.endian)

    if signed and res >> length - 1:
        res -= 1 << length
    return res


def int2ba(__i, length=None, endian=None, signed=False):
    """int2ba(int, /, length=None, endian=None, signed=False) -> bitarray

Convert the given integer to a bitarray (with given bit-endianness,
and no leading (big-endian) / trailing (little-endian) zeros), unless
the `length` of the bitarray is provided.  An `OverflowError` is raised
if the integer is not representable with the given number of bits.
`signed` determines whether two's complement is used to represent the integer,
and requires `length` to be provided.
"""
    if not isinstance(__i, int):
        raise TypeError("int expected, got '%s'" % type(__i).__name__)
    if length is not None:
        if not isinstance(length, int):
            raise TypeError("int expected for argument 'length'")
        if length <= 0:
            raise ValueError("length must be > 0")

    if signed:
        if length is None:
            raise TypeError("signed requires argument 'length'")
        m = 1 << length - 1
        if not (-m <= __i < m):
            raise OverflowError("signed integer not in range(%d, %d), "
                                "got %d" % (-m, m, __i))
        if __i < 0:
            __i += 1 << length
    else:  # unsigned
        if length and __i >> length:
            raise OverflowError("unsigned integer not in range(0, %d), "
                                "got %d" % (1 << length, __i))

    a = bitarray(0, endian)
    b = __i.to_bytes(bits2bytes(__i.bit_length()), byteorder=a.endian)
    a.frombytes(b)
    le = a.endian == 'little'
    if length is None:
        return strip(a, 'right' if le else 'left') if a else a + '0'

    if len(a) > length:
        return a[:length] if le else a[-length:]
    if len(a) == length:
        return a
    # len(a) < length, we need padding
    pad = zeros(length - len(a), a.endian)
    return a + pad if le else pad + a

# ------------------------------ Huffman coding -----------------------------

def _huffman_tree(__freq_map):
    """_huffman_tree(dict, /) -> Node

Given a dict mapping symbols to their frequency, construct a Huffman tree
and return its root node.
"""
    from heapq import heappush, heappop

    class Node(object):
        """
        There are to tyes of Node instances (both have 'freq' attribute):
          * leaf node: has 'symbol' attribute
          * parent node: has 'child' attribute (tuple with both children)
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
        # to construct a new parent node and push it onto the queue
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
bitarrays (with given bit-endianness).  Note that the symbols are not limited
to being strings.  Symbols may be any hashable object.
"""
    if not isinstance(__freq_map, dict):
        raise TypeError("dict expected, got '%s'" % type(__freq_map).__name__)

    if len(__freq_map) < 2:
        if len(__freq_map) == 0:
            raise ValueError("cannot create Huffman code with no symbols")
        # Only one symbol: Normally if only one symbol is given, the code
        # could be represented with zero bits.  However here, the code should
        # be at least one bit for the .encode() and .decode() methods to work.
        # So we represent the symbol by a single code of length one, in
        # particular one 0 bit.  This is an incomplete code, since if a 1 bit
        # is received, it has no meaning and will result in an error.
        sym = list(__freq_map)[0]
        return {sym: bitarray('0', endian)}

    result = {}

    def traverse(nd, prefix=bitarray(0, endian)):
        try:                    # leaf
            result[nd.symbol] = prefix
        except AttributeError:  # parent, so traverse each child
            traverse(nd.child[0], prefix + '0')
            traverse(nd.child[1], prefix + '1')

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
        except AttributeError:  # parent, so traverse each child
            traverse(nd.child[0], length + 1)
            traverse(nd.child[1], length + 1)

    traverse(_huffman_tree(__freq_map))

    # We now have a mapping of symbols to their code length, which is all we
    # need to construct a list of tuples (symbol, code length) sorted by
    # code length:
    table = sorted(code_length.items(), key=lambda item: item[1])

    maxbits = table[-1][1]
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
