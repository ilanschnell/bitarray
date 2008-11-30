"""
This module defines an object type which can efficiently represent
a bitarray.  Bitarrays are sequence types and behave very much like lists.

Please find a description of this package at:

    http://pypi.python.org/pypi/bitarray/

Author: Ilan Schnell
"""
__version__ = '0.3.3'

try:
    from _bitarray import _bitarray, bits2bytes, _sysinfo
except ImportError:
    raise ImportError("""No module named _bitarray

Are you running python from the root of the bitarray source tree?
If so, python is trying to import bitarray/_bitarray.so.
To resolve this problem, change to another directory.
""")

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
    pass


def test(verbosity=1):
    """test(verbosity=1)

Run self-test."""
    import test_bitarray
    return test_bitarray.run(verbosity=verbosity)
