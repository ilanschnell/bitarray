Comparison with bitstruct
=========================

This document compares :mod:`bitarray.bitfields` with the third-party
`bitstruct <https://pypi.org/project/bitstruct/>`__ package.  To run the
examples, first install ``bitstruct``::

    python -m pip install bitstruct

Both modules compile format strings and pack fixed-width values at arbitrary
bit boundaries.  There are also important differences:

* ``bitstruct`` packs to byte-padded ``bytes``; ``bitfields`` packs to an
  exact-length ``bitarray``.

* Their integer, float and padding codes overlap, but the codes for Boolean,
  raw-byte and bitarray fields differ.

* ``bitstruct`` has separate bit-order prefixes and a byte-order suffix.
  A ``bitfields`` prefix specifies the endianness of each field.

* ``bitstruct`` supports text, dictionaries, offsets and truncated input.
  ``bitfields`` supports named tuples, bitarrays, hexadecimal strings,
  comments and validating or explicit padding patterns.

The examples below use these imports:

.. code-block:: python

    >>> import bitstruct
    >>> from bitarray import bitarray, bitfields


Similarities
------------

Common fields
^^^^^^^^^^^^^

The ``u``, ``s``, ``f16``, ``f32``, ``f64``, ``p`` and ``P`` codes have the
same basic meaning.  ``bitstruct`` uses ``b`` for Boolean and ``r`` for raw
bytes; ``bitfields`` uses ``?`` and ``B``, respectively:

.. code-block:: python

    >>> struct_format = ">u3s5f16f32f64b1p3P2r16"
    >>> fields_format = ">u3s5f16f32f64?1p3P2B16"
    >>> values = (5, -3, 1.5, 1.875, -1.0, True, b"OK")
    >>> packed_struct = bitstruct.pack(struct_format, *values)
    >>> packed_fields = bitfields.pack(fields_format, *values)
    >>> bitstruct.calcsize(struct_format)
    142
    >>> len(packed_fields)
    142
    >>> packed_struct == bytes(packed_fields)
    True
    >>> bitstruct.unpack(struct_format, packed_struct) == values
    True
    >>> bitfields.unpack(fields_format, packed_fields) == values
    True


Compiled formats
^^^^^^^^^^^^^^^^

Both modules can compile a format for repeated use:

.. code-block:: python

    >>> struct_format = bitstruct.compile("u4s4")
    >>> fields_format = bitfields.compile(">u4s4")
    >>> packed_struct = struct_format.pack(10, -2)
    >>> packed_fields = fields_format.pack(10, -2)
    >>> packed_struct == bytes(packed_fields)
    True
    >>> struct_format.unpack(packed_struct)
    (10, -2)
    >>> fields_format.unpack(packed_fields)
    (10, -2)


Differences
-----------

Output and byte padding
^^^^^^^^^^^^^^^^^^^^^^^

``bitstruct`` returns ``bytes`` and pads the final byte.  ``bitfields``
returns a ``bitarray`` whose length is exactly the number of bits in the
format:

.. code-block:: python

    >>> packed_struct = bitstruct.pack("u3", 5)
    >>> packed_fields = bitfields.pack(">u3", 5)
    >>> type(packed_struct) is bytes
    True
    >>> type(packed_fields) is bitarray
    True
    >>> packed_struct
    b'\xa0'
    >>> packed_fields
    bitarray('101')
    >>> packed_fields.tobytes() == packed_struct
    True

``bitstruct`` ignores the five byte-padding bits when unpacking.  In contrast,
``bitfields`` requires an input bitarray of exactly the compiled width:

.. code-block:: python

    >>> bitstruct.unpack("u3", packed_struct)
    (5,)
    >>> try:
    ...     bitfields.unpack(">u3", bitarray(packed_struct, endian="big"))
    ... except ValueError:
    ...     print("wrong input length")
    wrong input length


Format defaults
^^^^^^^^^^^^^^^

``bitstruct`` requires every width and defaults to MSB-first order.
``bitfields`` permits omitted widths and defaults to little endian:

.. code-block:: python

    >>> try:
    ...     bitstruct.compile("u")
    ... except bitstruct.Error:
    ...     print("width required")
    width required
    >>> bitfields.compile("u").format()
    '<u1'
    >>> bitstruct.pack("u3", 3)
    b'`'
    >>> bitfields.pack("u3", 3).to01()
    '110'
    >>> bitfields.pack(">u3", 3).to01()
    '011'


Endianness syntax
^^^^^^^^^^^^^^^^^

The trailing ``<`` in a ``bitstruct`` format is its little-endian byte-order
suffix.  In ``bitfields``, ``<`` prefixes a field and selects its bit and byte
order.  For this byte-aligned integer, the two formats produce the same bytes:

.. code-block:: python

    >>> bitstruct.pack("u16<", 0x1234)
    b'4\x12'
    >>> bitfields.pack("<u16", 0x1234).tobytes()
    b'4\x12'

Prefixes persist for following fields in both format languages:

.. code-block:: python

    >>> bitstruct.pack(">u4u4", 4, 5)
    b'E'
    >>> bitfields.pack(">u4u4", 4, 5).tobytes()
    b'E'


Different field codes
^^^^^^^^^^^^^^^^^^^^^

Boolean fields use ``b`` in ``bitstruct`` and ``?`` in ``bitfields``:

.. code-block:: python

    >>> bitstruct.unpack("b1", bitstruct.pack("b1", True))
    (True,)
    >>> bitfields.unpack(">?", bitfields.pack(">?", True))
    (True,)

``bitstruct`` has text fields, whereas ``bitfields`` has bitarray and
hexadecimal-string fields:

.. code-block:: python

    >>> bitstruct.unpack("t16", bitstruct.pack("t16", "Hi"))
    ('Hi',)
    >>> bits = bitarray("101")
    >>> bitfields.unpack(">b3", bitfields.pack(">b3", bits))
    (bitarray('101'),)
    >>> bitfields.unpack(">h8", bitfields.pack(">h8", "aF"))
    ('af',)

Both can represent byte strings, using ``r`` in ``bitstruct`` and ``B`` in
``bitfields``:

.. code-block:: python

    >>> raw = b"Hi"
    >>> bitstruct.unpack("r16", bitstruct.pack("r16", raw))
    (b'Hi',)
    >>> bitfields.unpack("B16", bitfields.pack("B16", raw))
    (b'Hi',)


Padding
^^^^^^^

``bitstruct`` padding is ignored during unpacking:

.. code-block:: python

    >>> data = b"\xf5"
    >>> bitstruct.unpack("p4u4", data)
    (5,)

The ``p`` and ``P`` fields in ``bitfields`` are validated, while ``x`` and
``X`` are ignored:

.. code-block:: python

    >>> bits = bitarray(data, endian="big")
    >>> try:
    ...     bitfields.unpack(">p4 u4", bits)
    ... except ValueError:
    ...     print("padding mismatch")
    padding mismatch
    >>> bitfields.unpack(">x4 u4", bits)
    (5,)

Explicit padding patterns are specific to ``bitfields``:

.. code-block:: python

    >>> cf = bitfields.compile(">p{101} u2")
    >>> cf.pack(3)
    bitarray('10111')
    >>> cf.unpack(bitarray("10111"))
    (3,)


Named values
^^^^^^^^^^^^

``bitstruct`` keeps names outside its format and uses dictionaries:

.. code-block:: python

    >>> names = ["version", "length"]
    >>> values = {"version": 4, "length": 5}
    >>> packed_struct = bitstruct.pack_dict("u4u4", names, values)
    >>> bitstruct.unpack_dict("u4u4", names, packed_struct) == values
    True

``bitfields`` embeds names and returns a named tuple.  Packing remains
positional:

.. code-block:: python

    >>> cf = bitfields.compile(">u4{version} u4{length}")
    >>> packed_fields = cf.pack(4, 5)
    >>> fields = cf.unpack(packed_fields)
    >>> packed_fields.tobytes() == packed_struct
    True
    >>> fields
    Unpacked(version=4, length=5)
    >>> fields.version, fields.length
    (4, 5)


Offsets and truncated input
^^^^^^^^^^^^^^^^^^^^^^^^^^^

``bitstruct`` directly supports offsets into byte buffers and optionally
truncated input:

.. code-block:: python

    >>> buf = bytearray(2)
    >>> bitstruct.pack_into("u4u4", buf, 4, 10, 11)
    >>> buf
    bytearray(b'\n\xb0')
    >>> bitstruct.unpack_from("u4u4", buf, 4)
    (10, 11)
    >>> bitstruct.unpack("u8u8", b"\x12", allow_truncated=True)
    (18,)

With ``bitfields``, normal bitarray slicing handles an offset, and unpacking
always requires exactly the compiled number of bits:

.. code-block:: python

    >>> bits = bitarray(buf, endian="big")
    >>> bitfields.unpack(">u4 u4", bits[4:12])
    (10, 11)
    >>> try:
    ...     bitfields.unpack(">u8 u8", bitarray(b"\x12", endian="big"))
    ... except ValueError:
    ...     print("wrong input length")
    wrong input length


Bitfields format extensions
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Comments, embedded names and literal padding make larger ``bitfields``
formats self-documenting.  ``bitstruct`` does not support comments:

.. code-block:: python

    >>> cf = bitfields.compile("""
    ...     >h8{tag}       # hexadecimal tag
    ...      b3{flags}     # exact bitarray value
    ...      p{101}        # required marker bits
    ... """)
    >>> values = ("af", bitarray("010"))
    >>> packed = cf.pack(*values)
    >>> packed.to01()
    '10101111010101'
    >>> cf.unpack(packed) == values
    True
