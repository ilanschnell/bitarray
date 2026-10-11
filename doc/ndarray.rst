Bitarray and NumPy arrays
=========================

Bitarrays and NumPy arrays can both represent Boolean data, but use different
memory layouts.  A bitarray stores eight values per byte, whereas a NumPy
array with ``dtype=bool`` normally uses one byte per value.  Converting
between these representations therefore requires either expanding or packing
the data.

The examples below require NumPy:

.. code-block:: python

    >>> import numpy as np
    >>> from bitarray import bitarray
    >>> a = bitarray("10001100 11110001 10011", endian="big")
    >>> len(a)
    21


One byte per Boolean
--------------------

``bitarray.unpack()`` expands each bit into one byte.  ``np.frombuffer()``
then creates a Boolean view of those bytes without making another copy:

.. code-block:: python

    >>> b = np.frombuffer(a.unpack(), dtype=bool)
    >>> b.size, b.nbytes
    (21, 21)
    >>> bitarray(b.tolist()) == a
    True

The reverse operation first obtains the one-byte-per-value representation
from NumPy and then packs it into a bitarray:

.. code-block:: python

    >>> c = bitarray()
    >>> c.pack(b.tobytes())
    >>> c == a
    True

Although ``np.frombuffer()`` itself does not copy the expanded bytes,
``a.unpack()`` creates them.  The reverse conversion also copies data:
``b.tobytes()`` creates an expanded byte string, and ``c.pack()`` stores a
packed copy.  A zero-copy Boolean view is not possible because the two objects
assign a different amount of storage to each value.


Eight Booleans per byte
-----------------------

NumPy can also pack eight Boolean values into each byte.  Passing the
bitarray's endianness as NumPy's ``bitorder`` makes their byte representations
agree:

.. code-block:: python

    >>> packed = np.packbits(b, bitorder=a.endian)
    >>> packed.tolist()
    [140, 241, 152]
    >>> packed.tobytes() == bytes(a)
    True

The 21 values require three bytes.  The final three bits of the last byte are
padding.  ``count`` discards them when the NumPy array is unpacked:

.. code-block:: python

    >>> 8 * packed.size - len(a)  # number of padding bits
    3
    >>> unpacked = np.unpackbits(packed, count=len(a), bitorder=a.endian)
    >>> np.array_equal(unpacked, b)
    True

The bit order must also match for little-endian bitarrays:

.. code-block:: python

    >>> little = bitarray(a, endian="little")
    >>> np.packbits(b, bitorder="little").tobytes() == bytes(little)
    True

``np.packbits()`` and ``np.unpackbits()`` both create new arrays; they convert
between the compact and expanded representations rather than sharing memory.


Zero-copy byte views
--------------------

The packed bytes can be shared when they are viewed as bytes instead of as
individual Boolean values.  Since bitarrays export the buffer protocol,
``np.frombuffer()`` can make a NumPy ``uint8`` view of a bitarray's storage:

.. code-block:: python

    >>> a = bitarray("10001100 11110001", endian="big")
    >>> raw = np.frombuffer(a, dtype=np.uint8)
    >>> raw.tolist()
    [140, 241]
    >>> raw[0] = 0
    >>> a
    bitarray('0000000011110001')

The sharing also works in the other direction.  A bitarray can import the
contiguous buffer of a NumPy array:

.. code-block:: python

    >>> raw = np.array([0, 255], dtype=np.uint8)
    >>> a = bitarray(buffer=raw, endian="big")
    >>> a
    bitarray('0000000011111111')
    >>> a[:4] = 1
    >>> raw.tolist()
    [240, 255]

Changes made through either object are visible through the other.  While the
buffer is shared, operations that would resize it are prohibited.  See
`Buffer protocol <./buffer.rst>`__ for more about importing and exporting
buffers.
