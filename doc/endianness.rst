Bit-endianness
==============

Unless explicitly converting to machine representation, i.e. initializing
the buffer directly, using ``.tobytes()``, ``.frombytes()``, ``.tofile()``
or ``.fromfile()``, as well as using ``memoryview()``, the bit-endianness
will have no effect on any computation, and one can skip this section.

Since bitarrays allows addressing individual bits, where the machine
represents 8 bits in one byte, there are two obvious choices for this
mapping: little-endian and big-endian.

When dealing with the machine representation of bitarray objects, it is
recommended to always explicitly specify the endianness.

By default, bitarrays use big-endian representation:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> a = bitarray(b'A')
    >>> a.endian
    'big'
    >>> a
    bitarray('01000001')
    >>> a[6] = 1
    >>> a.tobytes()
    b'C'

Big-endian means that the most-significant bit comes first.
Here, ``a[0]`` is the lowest address (index) and most significant bit,
and ``a[7]`` is the highest address and least significant bit.

When creating a new bitarray object, the endianness can always be
specified explicitly:

.. code-block:: python

    >>> a = bitarray(b'A', endian='little')
    >>> a
    bitarray('10000010')
    >>> a.endian
    'little'

Here, the low-bit comes first because little-endian means that increasing
numeric significance corresponds to an increasing address.
So ``a[0]`` is the lowest address and least significant bit,
and ``a[7]`` is the highest address and most significant bit.

The bit-endianness is a property of the bitarray object.
The endianness cannot be changed once a bitarray object has been created.
When comparing bitarray objects, the endianness (and hence the machine
representation) is irrelevant; what matters is the mapping from indices
to bits:

.. code-block:: python

    >>> bitarray('11001', endian='big') == bitarray('11001', endian='little')
    True
    >>> a = bitarray(b'\x01', endian='little')
    >>> b = bitarray(b'\x80', endian='big')
    >>> a == b
    True
    >>> a.tobytes() == b.tobytes()
    False

Bitwise operations (``|``, ``^``, ``&=``, ``|=``, ``^=``, ``~``) are
implemented efficiently using the corresponding byte operations in C, i.e. the
operators act on the machine representation of the bitarray objects.
Therefore, it is not possible to perform bitwise operators on bitarrays
with different endianness.

As mentioned above, the endianness can not be changed once an object is
created.  However, you can create a new bitarray with different endianness:

.. code-block:: python

    >>> a = bitarray('111000', endian='little')
    >>> b = bitarray(a, endian='big')
    >>> b
    bitarray('111000')
    >>> a == b
    True


Utility functions
-----------------

A number of utility functions take into the bit-endianness into account.
For example consider:

.. code-block:: python

    >>> from bitarray.util import ba2int, int2ba
    >>> int2ba(12)
    bitarray('1100')

This is what one would normally expect, as Python's built-in ``bin()`` gives
the same result:

.. code-block:: python

    >>> bin(12)
    '0b1100'

However, this is only true because big-endian is the default bit-endianness.
When explicitly requesting a little-endian bitarray, we get:

.. code-block:: python

    >>> int2ba(12, endian="little")
    bitarray('0011')

Similarly, the function ``ba2int()`` takes into account the bit-endianness of
the bitarray it is provided with:

.. code-block:: python

    >>> a = bitarray("11001", "little")
    >>> ba2int(a)
    19
    >>> ba2int(bitarray(a, "big"))
    25

The same behavior is valid for ``hex2ba()``, ``ba2hex()``, ``base2ba()``
and ``ba2base()``.  Regardless of bit-endianness, these are always
inverse functions of each other:

.. code-block:: python

    >>> from bitarray.util import ba2hex, hex2ba, ba2base, base2ba
    >>> for endian in "little", "big":
    ...     a = bitarray("1010 0011 1110", endian)
    ...     assert int2ba(ba2int(a), len(a), a.endian) == a
    ...     assert hex2ba(ba2hex(a), a.endian) == a
    ...     assert base2ba(64, ba2base(64, a), a.endian) == a

or:

.. code-block:: python

    >>> for endian in "little", "big":
    ...     assert ba2int(int2ba(29, endian=endian)) == 29
    ...     assert ba2hex(hex2ba("e7a", endian)) == "e7a"
    ...     assert ba2base(64, base2ba(64, "h+E7", endian)) == "h+E7"
