Variable length bitarray format
===============================

In some cases, it is useful to represent bitarrays in a binary format that
is "self terminating" (in the same way that C strings are NUL terminated).
That is, when a bitarray of unknown length is encountered in a stream of
binary data, the format lets you know when the end of a bitarray is reached.
Such a "variable length format" (most memory efficient for small bitarrays)
is implemented in ``vl_encode()`` and ``vl_decode()``:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> from bitarray.util import vl_encode, vl_decode
    >>> a = bitarray('0110001111')
    >>> b = bitarray('001')
    >>> data = vl_encode(a) + vl_encode(b) + b'other stuff'
    >>> data
    b'\x96\x1e\x12other stuff'
    >>> stream = iter(data)
    >>> vl_decode(stream)      # the remaining stream is untouched
    bitarray('0110001111')
    >>> vl_decode(stream)
    bitarray('001')
    >>> bytes(stream)
    b'other stuff'

The variable length format is similar to LEB128.  A single byte can store
bitarrays up to 4 element, every additional byte stores up to 7 more elements.
The most significant bit of each byte indicated whether more bytes follow.
In addition, the first byte contains 3 bits which indicate the number of
padding bits at the end of the stream.  Here is an example of
encoding ``bitarray('01010100111001111')``:

.. code-block::

        01010100111001111        raw bitarray (of length 17)
        0101  0100111  001111    grouped (4, 7, 7, ...)
        0101  0100111  0011110   pad last group with zeros
     0010101  0100111  0011110   add number of pad bits (1) to front (001)
    10010101 10100111 00011110   add high bits (1, except 0 for last group)
        0x95     0xa7     0x1e   in hexadecimal - output stream

.. code-block:: python

    >>> vl_encode(bitarray('01010100111001111'))
    b'\x95\xa7\x1e'
