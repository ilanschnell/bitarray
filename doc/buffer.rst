Buffer protocol
---------------

Bitarray objects support the buffer protocol.  They can both export their
own buffer, as well as import another object's buffer.  Here is an example
where the bitarray's buffer is exported:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> a = bitarray('01000001 01000010 01000011', endian='big')
    >>> v = memoryview(a)
    >>> len(v)
    3
    >>> v[-1]
    67
    >>> v[:2].tobytes()
    b'AB'
    >>> v.readonly
    False
    >>> v[1] = 255
    >>> a
    bitarray('010000011111111101000011')

As of bitarray 2.3, it is also possible to import the buffer from an object
which exposes its buffer.  Here the ``bytearray``:

.. code-block:: python

    >>> b = bytearray([0x41, 0xff, 0x01])
    >>> a = bitarray(buffer=b)
    >>> a
    bitarray('010000011111111100000001')
    >>> a <<= 3  # shift all bits by 3 to the left
    >>> b
    bytearray(b'\x0f\xf8\x08')
    >>> a[20:] = 1
    >>> a
    bitarray('000011111111100000001111')

As bitarray's expose their buffer, we can create a bitarray which imports
the buffer from another bitarray.  Here we can two bitarrays which share the
same buffer:

.. code-block:: python

    >>> a = bitarray(32)
    >>> b = bitarray(buffer=a)
    >>> # the buffer address is the same!
    >>> assert a.buffer_info()[0] == b.buffer_info()[0]
    >>> a.setall(0)
    >>> assert a == b
    >>> b[::7] = 1
    >>> assert a == b
    >>> a
    bitarray('10000001000000100000010000001000')
