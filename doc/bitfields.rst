Bit-field structures
====================

Bitarray 3.12 added the ``bitarray.bitfields`` module, for packing and
unpacking fixed-width, bit-level structures.
The functionality of this module is similar to the ``struct`` module in
Python's standard library and the ``bitstruct`` package, with the main
difference that the ``bitfields`` module operates on ``bitarray`` objects.
Here is a detailed `comparison with bitstruct <./bitfields-bitstruct.rst>`__.
A format string describes a sequence of fields.  Values can be
packed into a bitarray and unpacked again without requiring byte alignment.


Basic usage
-----------

Use ``compile()`` to create an immutable, reusable ``Struct`` object:

.. code-block:: python

    >>> from bitarray.bitfields import compile
    >>> cf = compile("u3 s5 ? p2 h12 B16 f16")
    >>> values = (5, -3, False, "1fa", b"A\xff", 1.5)
    >>> a = cf.pack(*values)
    >>> a
    bitarray('1011011100010001111010110000010111111110000000001111100')
    >>> len(a) == cf.width
    True
    >>> cf.unpack(a) == values
    True

The compiled ``Struct`` object is immutable and picklable.
Its ``width`` attribute is the total number of bits in the structure,
and ``values`` is the number of values consumed by ``pack()`` and returned
by ``unpack()``.  Padding fields contribute to ``width`` but not to ``values``.

The module-level functions may be used without explicitly compiling a format:

.. code-block:: python

    >>> from bitarray.bitfields import pack, unpack, calcsize
    >>> calcsize(">u4 s5")  # total size in bits
    9
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

Except for comments, format strings are restricted to ASCII.
A ``#`` starts a comment that extends to the end of the line.
Comments may appear on their own line or after a field and may contain
non-ASCII characters.
A format is a sequence of field codes with optional widths.  Whitespace
between fields is optional.
The width defaults to one when omitted.  For ``h`` and ``B``,
it defaults to 4 and 8, respectively.
For a padding field with an explicit bit pattern, the width is inferred
from that pattern.

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

``p`` / ``P``
   Padding bits (``p`` zero / ``P`` one) validated during unpacking.  An
   explicit bitarray may be given in braces, for example ``p{110110}``.

``x`` / ``X``
   Padding bits (``x`` zero / ``X`` one) not validated.  These codes also
   accept an explicit bitarray in braces.

For an explicit padding bitarray, the width may be omitted and is then
inferred from the number of bits.  If supplied, the width must match.  The
explicit bits override the zero or one selected by the case of the code.
Thus, ``P{11011}`` and ``p5{110_11}`` are equivalent.  Their canonical format
is ``p5{11011}``; nonvalidating padding is similarly canonicalized to ``x``.

All fields must have a positive width.  The input to ``unpack()`` must have
exactly the compiled width.


Offsets
-------

The ``pack_into()`` and ``unpack_from()`` methods operate on a region of an
existing bitarray.  The offset is measured in bits; a negative offset counts
from the end of the bitarray.  ``pack_into()`` modifies a writable bitarray in
place without changing its length or the bits outside the selected region:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> cf = compile(">u4 u4")
    >>> a = bitarray(16)
    >>> cf.pack_into(a, 4, 10, 11)
    >>> a
    bitarray('0000101010110000')
    >>> cf.unpack_from(a, 4)
    (10, 11)
    >>> cf.unpack_from(a, -12)
    (10, 11)

The complete structure must fit in the bitarray starting at the given offset.
The module-level ``pack_into()`` and ``unpack_from()`` functions provide the
same operations without explicitly compiling the format.


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
    Unpacked(version=4, header_length=5)
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
has an explicit endian prefix; widths are explicit except for ``?``, whose
width is always one:

.. code-block:: python

    >>> compile("u3 >s5 x ?").format()
    '<u3 >s5 >x1 >?'

Compiling a canonical format produces an equal ``Struct`` object.


Reference
=========

``Struct(format)`` -> compiled struct object
   Central class for packing and unpacking bit-level structures.


``calcsize(format)`` -> int
   Return the size (in bits) of the struct corresponding to the format string.


``compile(format)`` -> Struct
   Compile given format string and return a compiled format object that
   can be used to pack and/or unpack data multiple times.


``pack(format, v1, v2, ...)`` -> bitarray
   Return a bitarray containing the values v1, v2, ... packed according
   to the format string.


``pack_into(format, bitarray, offset, v1, v2, ...)`` -> None
   Pack the values v1, v2, ... into the writable bitarray starting at bit
   offset ``offset``.  A negative offset counts from the end of bitarray.


``unpack(format, bitarray)`` -> tuple
   Return a tuple containing values unpacked according to the format string.


``unpack_from(format, bitarray, offset=0)`` -> tuple
   Unpack values from bitarray starting at bit offset ``offset``, and return a
   tuple.  A negative offset counts from the end of bitarray.


Struct methods:
^^^^^^^^^^^^^^^

``format()`` -> str
   Return the canonical format string reconstructed from this compiled format.


``pack(v1, v2, ...)`` -> bitarray
   Return a bitarray containing the values v1, v2, ... packed according to this
   compiled format.


``pack_into(bitarray, offset, v1, v2, ...)`` -> None
   Pack the values v1, v2, ... into the writable bitarray starting at bit
   offset ``offset``.  A negative offset counts from the end of bitarray.


``unpack(bitarray)`` -> tuple
   Return a tuple containing values unpacked according to this compiled format.


``unpack_from(bitarray, offset=0)`` -> tuple
   Unpack values from bitarray starting at bit offset ``offset``, and return a
   tuple.  A negative offset counts from the end of bitarray.


Struct attributes:
^^^^^^^^^^^^^^^^^^

``width`` -> int
   Total number of bits in the compiled structure.

``values`` -> int
   Number of values consumed by ``pack()`` and returned by ``unpack()``.
