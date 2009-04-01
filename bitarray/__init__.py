"""
This module defines an object type which can efficiently represent
a bitarray.  Bitarrays are sequence types and behave very much like lists.

Please find a description of this package at:

    http://pypi.python.org/pypi/bitarray/

Author: Ilan Schnell
"""
__version__ = '0.3.5'

from _bitarray import _bitarray, bits2bytes, _sysinfo


def _btree_insert(tree, sym, ba):
    """
    Insert symbol which is mapped to bitarray into tree
    """
    v = ba[0]
    if len(ba) > 1:
        if tree[v] == []:
            tree[v] = [[], []]
        _btree_insert(tree[v], sym, ba[1:])
    else:
        if tree[v] != []:
            raise ValueError("prefix code ambiguous")
        tree[v] = sym

def _mk_tree(codedict):
    # Generate tree from codedict
    tree = [[], []]
    for sym, ba in codedict.iteritems():
        _btree_insert(tree, sym, ba)
    return tree

def _check_codedict(codedict):
    if not isinstance(codedict, dict):
        raise TypeError("dictionary expected")
    if len(codedict) == 0:
        raise ValueError("prefix code empty")
    for k, v in codedict.iteritems():
        if not isinstance(v, bitarray):
            raise TypeError("bitarray expected for dictionary value")
        if v.length() == 0:
            raise ValueError("non-empty bitarray expected")

class bitarray(_bitarray):
    """bitarray([initial][endian=string])

Return a new bitarray object whose items are bits initialized from
the optional initial, and endianness.
If no object is provided, the bitarray is initialized to have length zero.
The initial object may be of the following types:

int, long
    Create bitarray of length given by the integer.  The initial values
    in the array are random, because only the memory allocated.

string
    Create bitarray from a string of '0's and '1's.

list, tuple, iterable
    Create bitarray from a sequence, each element in the sequence is
    converted to a bit using truth value value.

bitarray
    Create bitarray from another bitarray.  This is done by copying the
    memory holding the bitarray data, and is hence very fast.

The optional keyword arguments 'endian' specifies the bit endianness of the
created bitarray object.
Allowed values are 'big' and 'little' (default is 'big').

Note that setting the bit endianness only has an effect when accessing the
machine representation of the bitarray, i.e. when using the methods: tofile,
fromfile, tostring, fromstring."""

    def decode(self, codedict):
        """decode(code)

Given a prefix code (a dict mapping symbols to bitarrays),
decode the content of the bitarray and return the list of symbols."""
        _check_codedict(codedict)
        return self._decode(_mk_tree(codedict))

    def encode(self, codedict, iterable):
        """encode(code, iterable)

Given a prefix code (a dict mapping symbols to bitarrays),
iterates over iterable object with symbols, and extends the bitarray
with the corresponding bitarray for each symbols."""
        _check_codedict(codedict)
        return self._encode(codedict, iterable)

    def search(self, x, limit=-1):
        """search(x[, limit])

Given a bitarray x (or an object which can be converted to a bitarray),
returns the start positions of x matching self as a list.
The optional argument limits the number of search results to the integer
specified.  By default, all search results are returned."""
        return self._search(bitarray(x), limit)

    def __contains__(self, other):
        """__contains__(x)

Return True if bitarray contains x, False otherwise.
If x is an integer (which includes booleans), it is determined
whether or not the corresponding bit is contained in the bitarray.
If x is an object which can be cast into a bitarray, such as e.g.
the string '0110', a list, or a bitarray itself, a sequential search
will be performed to determine return value."""
        if isinstance(other, int):
            try:
                self.index(other)
                return True
            except ValueError:
                return False
        else:
            return bool(self._search(bitarray(other), 1))


def test(verbosity=1):
    """test(verbosity=1)

Run self-test."""
    import test_bitarray
    return test_bitarray.run(verbosity=verbosity)
