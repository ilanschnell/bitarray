Reference
=========

bitarray version: 3.4.0 -- `change log <https://github.com/ilanschnell/bitarray/blob/master/doc/changelog.rst>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.

Also, ``sub_bitarray`` refers to either a bitarray, or an ``item``.


The bitarray object:
--------------------

``bitarray(initializer=0, /, endian='big', buffer=None)`` -> bitarray
   Return a new bitarray object whose items are bits initialized from
   the optional initializer, and bit-endianness.
   The initializer may be one of the following types:
   a.) ``int`` bitarray, initialized to zeros, of given length
   b.) ``bytes`` or ``bytearray`` to initialize buffer directly
   c.) ``str`` of 0s and 1s, ignoring whitespace and "_"
   d.) iterable of integers 0 or 1.

   Optional keyword arguments:

   ``endian``: Specifies the bit-endianness of the created bitarray object.
   Allowed values are ``big`` and ``little`` (the default is ``big``).
   The bit-endianness effects the buffer representation of the bitarray.

   ``buffer``: Any object which exposes a buffer.  When provided, ``initializer``
   cannot be present (or has to be ``None``).  The imported buffer may be
   read-only or writable, depending on the object type.

   New in version 2.3: optional ``buffer`` argument

   New in version 3.4: allow initializer ``bytes`` or ``bytearray`` to set buffer directly


bitarray methods:
-----------------

``all()`` -> bool
   Return True when all bits in bitarray are True.
   Note that ``a.all()`` is faster than ``all(a)``.


``any()`` -> bool
   Return True when any bit in bitarray is True.
   Note that ``a.any()`` is faster than ``any(a)``.


``append(item, /)``
   Append ``item`` to the end of the bitarray.


``buffer_info()`` -> tuple
   Return a tuple containing:

   0. memory address of buffer
   1. buffer size (in bytes)
   2. bit-endianness as a Unicode string
   3. number of pad bits
   4. allocated memory for the buffer (in bytes)
   5. memory is read-only
   6. buffer is imported
   7. number of buffer exports


``bytereverse(start=0, stop=<end of buffer>, /)``
   For each byte in byte-range(start, stop) reverse bits in-place.
   The start and stop indices are given in terms of bytes (not bits).
   Also note that this method only changes the buffer; it does not change the
   bit-endianness of the bitarray object.  Pad bits are left unchanged such
   that two consecutive calls will always leave the bitarray unchanged.

   New in version 2.2.5: optional start and stop arguments


``clear()``
   Remove all items from the bitarray.

   New in version 1.4


``copy()`` -> bitarray
   Return a copy of the bitarray.


``count(value=1, start=0, stop=<end>, step=1, /)`` -> int
   Number of occurrences of ``value`` bitarray within ``[start:stop:step]``.
   Optional arguments ``start``, ``stop`` and ``step`` are interpreted in
   slice notation, meaning ``a.count(value, start, stop, step)`` equals
   ``a[start:stop:step].count(value)``.
   The ``value`` may also be a sub-bitarray.  In this case non-overlapping
   occurrences are counted within ``[start:stop]`` (``step`` must be 1).

   New in version 1.1.0: optional start and stop arguments

   New in version 2.3.7: optional step argument

   New in version 2.9: add non-overlapping sub-bitarray count


``decode(code, /)`` -> iterator
   Given a prefix code (a dict mapping symbols to bitarrays, or ``decodetree``
   object), decode content of bitarray and return an iterator over
   corresponding symbols.

   See also: `Bitarray 3 transition <https://github.com/ilanschnell/bitarray/blob/master/doc/bitarray3.rst>`__

   New in version 3.0: returns iterator (equivalent to past ``.iterdecode()``)


``encode(code, iterable, /)``
   Given a prefix code (a dict mapping symbols to bitarrays),
   iterate over the iterable object with symbols, and extend bitarray
   with corresponding bitarray for each symbol.


``extend(iterable, /)``
   Append items from to the end of the bitarray.
   If ``iterable`` is a Unicode string, each ``0`` and ``1`` are appended as
   bits (ignoring whitespace and underscore).

   New in version 3.4: allow ``bytes`` object


``fill()`` -> int
   Add zeros to the end of the bitarray, such that the length will be
   a multiple of 8, and return the number of bits added [0..7].


``find(sub_bitarray, start=0, stop=<end>, /, right=False)`` -> int
   Return lowest (or rightmost when ``right=True``) index where sub_bitarray
   is found, such that sub_bitarray is contained within ``[start:stop]``.
   Return -1 when sub_bitarray is not found.

   New in version 2.1

   New in version 2.9: add optional keyword argument ``right``


``frombytes(bytes, /)``
   Extend bitarray with raw bytes from a bytes-like object.
   Each added byte will add eight bits to the bitarray.

   New in version 2.5.0: allow bytes-like argument


``fromfile(f, n=-1, /)``
   Extend bitarray with up to ``n`` bytes read from file object ``f`` (or any
   other binary stream what supports a ``.read()`` method, e.g. ``io.BytesIO``).
   Each read byte will add eight bits to the bitarray.  When ``n`` is omitted or
   negative, all bytes until EOF is reached.  When ``n`` is non-negative but
   exceeds the data available, ``EOFError`` is raised (but the available data
   is still read and appended).


``index(sub_bitarray, start=0, stop=<end>, /, right=False)`` -> int
   Return lowest (or rightmost when ``right=True``) index where sub_bitarray
   is found, such that sub_bitarray is contained within ``[start:stop]``.
   Raises ``ValueError`` when the sub_bitarray is not present.

   New in version 2.9: add optional keyword argument ``right``


``insert(index, value, /)``
   Insert ``value`` into bitarray before ``index``.


``invert(index=<all bits>, /)``
   Invert all bits in bitarray (in-place).
   When the optional ``index`` is given, only invert the single bit at index.

   New in version 1.5.3: optional index argument


``pack(bytes, /)``
   Extend bitarray from a bytes-like object, where each byte corresponds
   to a single bit.  The byte ``b'\x00'`` maps to bit 0 and all other bytes
   map to bit 1.

   This method, as well as the ``.unpack()`` method, are meant for efficient
   transfer of data between bitarray objects to other Python objects (for
   example NumPy's ndarray object) which have a different memory view.

   New in version 2.5.0: allow bytes-like argument


``pop(index=-1, /)`` -> item
   Remove and return item at ``index`` (default last).
   Raises ``IndexError`` if index is out of range.


``remove(value, /)``
   Remove the first occurrence of ``value``.
   Raises ``ValueError`` if value is not present.


``reverse()``
   Reverse all bits in bitarray (in-place).


``search(sub_bitarray, start=0, stop=<end>, /, right=False)`` -> iterator
   Return iterator over indices where sub_bitarray is found, such that
   sub_bitarray is contained within ``[start:stop]``.
   The indices are iterated in ascending order (from lowest to highest),
   unless ``right=True``, which will iterate in descending order (starting with
   rightmost match).

   See also: `Bitarray 3 transition <https://github.com/ilanschnell/bitarray/blob/master/doc/bitarray3.rst>`__

   New in version 2.9: optional start and stop arguments - add optional keyword argument ``right``

   New in version 3.0: returns iterator (equivalent to past ``.itersearch()``)


``setall(value, /)``
   Set all elements in bitarray to ``value``.
   Note that ``a.setall(value)`` is equivalent to ``a[:] = value``.


``sort(reverse=False)``
   Sort all bits in bitarray (in-place).


``to01(group=0, sep=' ')`` -> str
   Return bitarray as Unicode string of '0's and '1's.
   The bits are grouped into ``group`` bits (default is no grouping).
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   New in version 3.3: optional ``group`` and ``sep`` arguments


``tobytes()`` -> bytes
   Return the bitarray buffer in bytes (pad bits are set to zero).


``tofile(f, /)``
   Write byte representation of bitarray to file object f.


``tolist()`` -> list
   Return bitarray as list of integers.
   ``a.tolist()`` equals ``list(a)``.

   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the bitarray object,
   which may cause a memory error if the bitarray is very large.


``unpack(zero=b'\x00', one=b'\x01')`` -> bytes
   Return bytes that contain one byte for each bit in the bitarray,
   using specified mapping.


bitarray data descriptors:
--------------------------

Data descriptors were added in version 2.6.

``endian`` -> str
   bit-endianness as Unicode string

   New in version 3.4: replaces former ``.endian()`` method


``nbytes`` -> int
   buffer size in bytes


``padbits`` -> int
   number of pad bits


``readonly`` -> bool
   bool indicating whether buffer is read-only


Other objects:
--------------

``frozenbitarray(initializer=0, /, endian='big', buffer=None)`` -> frozenbitarray
   Return a ``frozenbitarray`` object.  Initialized the same way a ``bitarray``
   object is initialized.  A ``frozenbitarray`` is immutable and hashable,
   and may therefore be used as a dictionary key.

   New in version 1.1


``decodetree(code, /)`` -> decodetree
   Given a prefix code (a dict mapping symbols to bitarrays),
   create a binary tree object to be passed to ``.decode()``.

   New in version 1.6


Functions defined in the `bitarray` module:
-------------------------------------------

``bits2bytes(n, /)`` -> int
   Return the number of bytes necessary to store n bits.


``get_default_endian()`` -> str
   Return the default bit-endianness for new bitarray objects being created.
   Unless ``_set_default_endian('little')`` was called, the default
   bit-endianness is ``big``.

   New in version 1.3


``test(verbosity=1)`` -> TextTestResult
   Run self-test, and return unittest.runner.TextTestResult object.


Functions defined in `bitarray.util` module:
--------------------------------------------

This sub-module was added in version 1.2.

``any_and(a, b, /)`` -> bool
   Efficient implementation of ``any(a & b)``.

   New in version 2.7


``ba2base(n, bitarray, /, group=0, sep=' ')`` -> str
   Return a string containing the base ``n`` ASCII representation of
   the bitarray.  Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   The bitarray has to be multiple of length 1, 2, 3, 4, 5 or 6 respectively.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.9

   New in version 3.3: optional ``group`` and ``sep`` arguments


``ba2hex(bitarray, /, group=0, sep=' ')`` -> hexstr
   Return a string containing the hexadecimal representation of
   the bitarray (which has to be multiple of 4 in length).
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   New in version 3.3: optional ``group`` and ``sep`` arguments


``ba2int(bitarray, /, signed=False)`` -> int
   Convert the given bitarray to an integer.
   The bit-endianness of the bitarray is respected.
   ``signed`` indicates whether two's complement is used to represent the integer.


``base2ba(n, asciistr, /, endian=None)`` -> bitarray
   Bitarray of base ``n`` ASCII representation.
   Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.  Whitespace is ignored.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.9

   New in version 3.3: ignore whitespace


``byteswap(a, /, n=<buffer size>)``
   Reverse every ``n`` consecutive bytes of ``a`` in-place.
   By default, all bytes are reversed.  Note that ``n`` is not limited to 2, 4
   or 8, but can be any positive integer.
   Also, ``a`` may be any object that exposes a writeable buffer.
   Nothing about this function is specific to bitarray objects.

   New in version 3.4


``canonical_decode(bitarray, count, symbol, /)`` -> iterator
   Decode bitarray using canonical Huffman decoding tables
   where ``count`` is a sequence containing the number of symbols of each length
   and ``symbol`` is a sequence of symbols in canonical order.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/bitarray/blob/master/doc/canonical.rst>`__

   New in version 2.5


``canonical_huffman(dict, /)`` -> tuple
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the canonical Huffman code.  Returns a tuple containing:

   0. the canonical Huffman code as a dict mapping symbols to bitarrays
   1. a list containing the number of symbols of each code length
   2. a list of symbols in canonical order

   Note: the two lists may be used as input for ``canonical_decode()``.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/bitarray/blob/master/doc/canonical.rst>`__

   New in version 2.5


``correspond_all(a, b, /)`` -> tuple
   Return tuple with counts of: ~a & ~b, ~a & b, a & ~b, a & b

   New in version 3.4


``count_and(a, b, /)`` -> int
   Return ``(a & b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.


``count_n(a, n, value=1, /)`` -> int
   Return lowest index ``i`` for which ``a[:i].count(value) == n``.
   Raises ``ValueError`` when ``n`` exceeds total count (``a.count(value)``).

   New in version 2.3.6: optional value argument


``count_or(a, b, /)`` -> int
   Return ``(a | b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.


``count_xor(a, b, /)`` -> int
   Return ``(a ^ b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.

   This is also known as the Hamming distance.


``deserialize(bytes, /)`` -> bitarray
   Return a bitarray given a bytes-like representation such as returned
   by ``serialize()``.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.8

   New in version 2.5.0: allow bytes-like argument


``hex2ba(hexstr, /, endian=None)`` -> bitarray
   Bitarray of hexadecimal representation.  hexstr may contain any number
   (including odd numbers) of hex digits (upper or lower case).
   Whitespace is ignored.

   New in version 3.3: ignore whitespace


``huffman_code(dict, /, endian=None)`` -> dict
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the Huffman code, i.e. a dict mapping those symbols to
   bitarrays (with given bit-endianness).  Note that the symbols are not limited
   to being strings.  Symbols may be any hashable object.


``int2ba(int, /, length=None, endian=None, signed=False)`` -> bitarray
   Convert the given integer to a bitarray (with given bit-endianness,
   and no leading (big-endian) / trailing (little-endian) zeros), unless
   the ``length`` of the bitarray is provided.  An ``OverflowError`` is raised
   if the integer is not representable with the given number of bits.
   ``signed`` determines whether two's complement is used to represent the integer,
   and requires ``length`` to be provided.


``intervals(bitarray, /)`` -> iterator
   Compute all uninterrupted intervals of 1s and 0s, and return an
   iterator over tuples ``(value, start, stop)``.  The intervals are guaranteed
   to be in order, and their size is always non-zero (``stop - start > 0``).

   New in version 2.7


``ones(n, /, endian=None)`` -> bitarray
   Create a bitarray of length ``n``, with all values ``1``, and optional
   bit-endianness (``little`` or ``big``).

   New in version 2.9


``parity(a, /)`` -> int
   Return parity of bitarray ``a``.
   ``parity(a)`` is equivalent to ``a.count() % 2`` but more efficient.

   New in version 1.9


``pprint(bitarray, /, stream=None, group=8, indent=4, width=80)``
   Prints the formatted representation of object on ``stream`` (which defaults
   to ``sys.stdout``).  By default, elements are grouped in bytes (8 elements),
   and 8 bytes (64 elements) per line.
   Non-bitarray objects are printed by the standard library
   function ``pprint.pprint()``.

   New in version 1.8


``sc_decode(stream)`` -> bitarray
   Decompress binary stream (an integer iterator, or bytes-like object) of a
   sparse compressed (``sc``) bitarray, and return the decoded  bitarray.
   This function consumes only one bitarray and leaves the remaining stream
   untouched.  Use ``sc_encode()`` for compressing (encoding).

   See also: `Compression of sparse bitarrays <https://github.com/ilanschnell/bitarray/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``sc_encode(bitarray, /)`` -> bytes
   Compress a sparse bitarray and return its binary representation.
   This representation is useful for efficiently storing sparse bitarrays.
   Use ``sc_decode()`` for decompressing (decoding).

   See also: `Compression of sparse bitarrays <https://github.com/ilanschnell/bitarray/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``serialize(bitarray, /)`` -> bytes
   Return a serialized representation of the bitarray, which may be passed to
   ``deserialize()``.  It efficiently represents the bitarray object (including
   its bit-endianness) and is guaranteed not to change in future releases.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.8


``strip(bitarray, /, mode='right')`` -> bitarray
   Return a new bitarray with zeros stripped from left, right or both ends.
   Allowed values for mode are the strings: ``left``, ``right``, ``both``


``subset(a, b, /)`` -> bool
   Return ``True`` if bitarray ``a`` is a subset of bitarray ``b``.
   ``subset(a, b)`` is equivalent to ``a | b == b`` (and equally ``a & b == a``) but
   more efficient as no intermediate bitarray object is created and the buffer
   iteration is stopped as soon as one mismatch is found.


``urandom(length, /, endian=None)`` -> bitarray
   Return a bitarray of ``length`` random bits (uses ``os.urandom``).

   New in version 1.7


``vl_decode(stream, /, endian=None)`` -> bitarray
   Decode binary stream (an integer iterator, or bytes-like object), and
   return the decoded bitarray.  This function consumes only one bitarray and
   leaves the remaining stream untouched.  Use ``vl_encode()`` for encoding.

   See also: `Variable length bitarray format <https://github.com/ilanschnell/bitarray/blob/master/doc/variable_length.rst>`__

   New in version 2.2


``vl_encode(bitarray, /)`` -> bytes
   Return variable length binary representation of bitarray.
   This representation is useful for efficiently storing small bitarray
   in a binary stream.  Use ``vl_decode()`` for decoding.

   See also: `Variable length bitarray format <https://github.com/ilanschnell/bitarray/blob/master/doc/variable_length.rst>`__

   New in version 2.2


``xor_indices(a, /)`` -> int
   Return xor reduced indices of all active bits in bitarray ``a``.
   This is essentially equivalent to
   ``reduce(operator.xor, [i for i, v in enumerate(a) if v])``.

   New in version 3.2


``zeros(n, /, endian=None)`` -> bitarray
   Create a bitarray of length ``n``, with all values ``0``, and optional
   bit-endianness (``little`` or ``big``).


