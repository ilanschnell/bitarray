"""
Useful utilities for working with bitarrays.

Not sure if I should put this into the bitarray package itself :-/
"""
import sys
import binascii

from bitarray import _bitarray, bitarray


__all__ = ['frozenbitarray', 'zeros', 'ba2hex', 'hex2ba',
           'ba2int', 'int2ba']


class frozenbitarray(_bitarray):
    """frozenbitarray(initial=0, /, endian='big')

Return a frozenbitarray object, which is initialized the same way a bitarray
object is initialized.  A frozenbitarray is immutable and hashable.
Its contents cannot be altered after is created; however, it can be used as
a dictionary key.
"""
    def __repr__(self):
        return 'frozen' + _bitarray.__repr__(self)

    def __hash__(self):
        if getattr(self, '_hash', None) is None:
            self._hash = hash((self.length(), self.tobytes()))
        return self._hash

    def __delitem__(self, *args, **kwargs):
        raise TypeError("'frozenbitarray' is immutable")

    append = bytereverse = extend = encode = fill = __delitem__
    frombytes = fromfile = insert = invert = pack = pop = __delitem__
    remove = reverse = setall = sort = __setitem__ = __delitem__
    __iand__ = __iadd__ = __imul__ = __ior__ = __ixor__ = __delitem__


def zeros(length, endian='big'):
    """zeros(length, /, endian='big') -> bitarray

Create a bitarray of length, with all values 0.
"""
    if not isinstance(length,
                      (int, long) if sys.version_info[0] == 2 else int):
        raise TypeError("integer expected")
    if length < 0:
        raise ValueError("non-negative integer expected")
    a = bitarray(length, endian)
    a.setall(0)
    return a


def ba2hex(a):
    """ba2hex(bitarray, /) -> hexstr

Return a bytes object containing with hexadecimal representation of
the bitarray (which has to be multiple of 4 in length).
"""
    if not isinstance(a, (bitarray, frozenbitarray)):
        raise TypeError("bitarray expected")
    if a.endian() != 'big':
        raise ValueError("big endian bitarray expected")
    la = len(a)
    if la % 4:
        raise ValueError("bitarray length not multiple of 4")
    if la % 8:
        # make sure we don't mutate the original argument
        a = a + bitarray(4)
    assert len(a) % 8 == 0
    s = binascii.hexlify(a.tobytes())
    if la % 8:
        s = s[:-1]
    return s


def hex2ba(s):
    """hex2ba(hexstr, /) -> bitarray

Bitarray of hexadecimal representation.
hexstr may contain any number of hex digits (upper or lower case).
"""
    if not isinstance(s, (str, bytes)):
        raise TypeError("string expected")
    ls = len(s)
    if ls % 2:
        s = s + ('0' if isinstance(s, str) else b'0')
    assert len(s) % 2 == 0
    a = bitarray()
    a.frombytes(binascii.unhexlify(s))
    if ls % 2:
        del a[-4:]
    return a


def ba2int(a):
    """ba2int(bitarray, /) -> int

Convert the given bitarray into an integer.
"""
    if not isinstance(a, (bitarray, frozenbitarray)):
        raise TypeError("bitarray expected")
    if a.endian() != 'big':
        raise ValueError("big endian bitarray expected")
    if len(a) == 0:
        raise ValueError("non-empty bitarray expected")
    # pad with leadind zeros, such that length is multiple of 8
    b = bitarray((8 - len(a) % 8) * '0') + a
    assert len(b) % 8 == 0
    res, m = 0, 1
    c = bytearray(b.tobytes())
    c.reverse()
    for x in c:
        res += x * m
        m *= 256
    return res


def int2ba(i, length=None):
    """int2ba(int, /, length=None) -> bitarray

Convert the given integer into a bitarray (with no leading zeros).
If length is provided, the result will be of this length, and an
OverflowError will be raised, if the integer cannot be represented
within length bits.
"""
    if not isinstance(i, (int, long) if sys.version_info[0] == 2 else int):
        raise TypeError("integer expected")
    if i < 0:
        raise ValueError("non-negative integer expected")
    if length is not None:
        if not isinstance(length, int):
            raise TypeError("integer expected")
        if length <= 0:
            raise ValueError("integer larger than 0 expected")
    if i == 0:
        return zeros(length or 1)
    b = bytearray()
    while i:
        i, r = divmod(i, 256)
        b.append(r)
    b.reverse()
    a = bitarray()
    a.frombytes(bytes(b))
    fa = a.index(1)
    if length is None:
        if fa > 0:
            return a[fa:]
        else:
            return a
    else:
        la = len(a)
        if la - fa > length:
            raise OverflowError("cannot represent integer in "
                                "%d bits" % length)
        if la == length:
            return a
        if la < length:
            return zeros(length - la) + a
        if la > length:
            return a[la - length:]
