Bit-field structures
====================

Bitarray 3.12 added the ``bitarray.bitfields`` module, for packing and
unpacking fixed-width, bit-level structures.
The functionality of this module is similar to the ``struct`` module in
Python's standard library and the ``bitstruct`` package, with the main
difference that the ``bitfields`` module operates on ``bitarray`` objects.
A format string describes a sequence of fields.  Values can be
packed into a bitarray and unpacked again without requiring byte alignment.


Basic usage
-----------

Use ``compile()`` to create an immutable, reusable ``Struct`` object:

.. code-block:: python

    >>> from bitarray.bitfields import compile
    >>> cf = compile("u3 s5 ? x2 h12 B16 f16")
    >>> values = (5, -3, False, "1fa", b"A\xff", 1.5)
    >>> a = cf.pack(*values)
    >>> a
    bitarray('1011011100010001111010110000010111111110000000001111100')
    >>> len(a) == cf.width
    True
    >>> cf.unpack(a) == values
    True

The compiled ``Struct`` object is immutable.  Its ``width`` attribute is the
total number of bits in the structure, and ``values`` is the number of values
consumed by ``pack()`` and returned by ``unpack()``.  Padding fields contribute
to ``width`` but not to ``values``.

The module-level functions may be used without explicitly compiling a format:

.. code-block:: python

    >>> from bitarray.bitfields import pack, unpack
    >>> a = pack(">u4 s5", 10, -2)
    >>> a
    bitarray('101011110')
    >>> unpack(">u4 s5", a)
    (10, -2)

Compiled versions of the most recent format strings passed to the module-level
functions are cached.  Programs that use only a few format strings therefore
need not retain and reuse a single ``Struct`` instance.


Format strings
--------------

Format strings are restricted to ASCII.
A format is a sequence of field codes with optional widths.  Whitespace
between fields is optional.  The width defaults to one when omitted.

``u``
   An unsigned integer stored in the field width.

``s``
   A signed integer stored in the field width using two's-complement
   representation.

``?``
   A Boolean value stored in a field of width one.  Packing uses normal Python
   truth-value testing; unpacking returns ``bool``.

``f``
   An IEEE floating-point value.  The width must be 16, 32, or 64.

``h``
   A hexadecimal string occupying the field width.  The width must be a
   multiple of four.  Packing is case-insensitive and ignores whitespace;
   unpacking returns lowercase.

``b``
   A bitarray whose length matches the field width.

``B``
   A ``bytes`` or ``bytearray`` value occupying the field width.  The width
   must be a multiple of eight; unpacking always returns ``bytes``.

``x``
   Zero-padding bits.

``X``
   One-padding bits.

All fields must have a positive width.  The input to ``unpack()`` must have
exactly the compiled width.  Padding bits are skipped during unpacking and are
not validated.


Named fields
------------

Value-producing fields may be named by appending ``{name}``, for example
``u4{version}``.  If one field is named, all value-producing fields must be
named, and the names must be unique identifiers that are not Python
keywords and do not begin with an underscore.  Padding fields cannot be
named.  For a named format, ``unpack()`` returns a named tuple whose values are
also accessible as attributes; ``pack()`` continues to accept values
positionally.

.. code-block:: python

    >>> cf = compile(">u4{version} u4{header_length}")
    >>> fields = cf.unpack(cf.pack(4, 5))
    >>> fields
    Result(version=4, header_length=5)
    >>> fields.version
    4


Endianness
----------

Prefix a field with ``<`` for little-endian or ``>`` for big-endian bit and
byte order.  The selected order applies to that field and all following fields
until another prefix changes it.  The initial order is little-endian.

.. code-block:: python

    >>> cf = compile(">u3 s5 <h8 B16")
    >>> cf.format()
    '>u3 >s5 <h8 <B16'

Formats may therefore mix little- and big-endian fields.  The bitarray returned
by ``pack()`` has the endianness of the first field; an empty format produces a
little-endian bitarray.  ``unpack()`` accepts an input bitarray of either
endianness and interprets its logical bit sequence according to the format.


Canonical formats
-----------------

``Struct.format()`` returns a canonical representation in which every field
has an explicit endian prefix and width:

.. code-block:: python

    >>> compile("u3 >s5 x").format()
    '<u3 >s5 >x1'

Compiling a canonical format produces an equal ``Struct`` object.
