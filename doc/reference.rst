Reference
=========

bitarray version: 2.5.1 -- `change log <https://github.com/ilanschnell/bitarray/blob/master/doc/changelog.rst>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.


The bitarray object:
--------------------

``bitarray(initializer=0, /, endian='big', buffer=None)`` -> bitarray
   Return a new bitarray object whose items are bits initialized from
   the optional initial object, and endianness.
   The initializer may be of the following types:

   ``int``: Create a bitarray of given integer length.  The initial values are
   uninitialized.

   ``str``: Create bitarray from a string of ``0`` and ``1``.

   ``iterable``: Create bitarray from iterable or sequence or integers 0 or 1.

   Optional keyword arguments:

   ``endian``: Specifies the bit endianness of the created bitarray object.
   Allowed values are ``big`` and ``little`` (the default is ``big``).
   The bit endianness effects the buffer representation of the bitarray.

   ``buffer``: Any object which exposes a buffer.  When provided, ``initializer``
   cannot be present (or has to be ``None``).  The imported buffer may be
   readonly or writable, depending on the object type.

   New in version 2.3: optional ``buffer`` argument.


bitarray methods:
-----------------

``all()`` -> bool
   Return True when all bits in the array are True.
   Note that ``a.all()`` is faster than ``all(a)``.


``any()`` -> bool
   Return True when any bit in the array is True.
   Note that ``a.any()`` is faster than ``any(a)``.


``append(item, /)``
   Append ``item`` to the end of the bitarray.


``buffer_info()`` -> tuple
   Return a tuple containing:

   0. memory address of buffer
   1. buffer size (in bytes)
   2. bit endianness as a string
   3. number of unused padding bits
   4. allocated memory for the buffer (in bytes)
   5. memory is read-only
   6. buffer is imported
   7. number of buffer exports


``bytereverse(start=0, stop=<end of buffer>, /)``
   Reverse the bit order for each buffer byte in range(start, stop) in-place.
   The start and stop indices are given in terms of bytes (not bits).
   Also note that this method only changes the buffer; it does not change the
   endianness of the bitarray object.

   New in version 2.2.5: optional start and stop arguments.


``clear()``
   Remove all items from the bitarray.

   New in version 1.4.


``copy()`` -> bitarray
   Return a copy of the bitarray.


``count(value=1, start=0, stop=<end of array>, step=1, /)`` -> int
   Count the number of occurrences of ``value`` in the bitarray.

   New in version 1.1.0: optional start and stop arguments.

   New in version 2.3.7: optional step argument.


``decode(code, /)`` -> list
   Given a prefix code (a dict mapping symbols to bitarrays, or ``decodetree``
   object), decode the content of the bitarray and return it as a list of
   symbols.


``encode(code, iterable, /)``
   Given a prefix code (a dict mapping symbols to bitarrays),
   iterate over the iterable object with symbols, and extend the bitarray
   with the corresponding bitarray for each symbol.


``endian()`` -> str
   Return the bit endianness of the bitarray as a string (``little`` or ``big``).


``extend(iterable, /)``
   Append all items from ``iterable`` to the end of the bitarray.
   If the iterable is a string, each ``0`` and ``1`` are appended as
   bits (ignoring whitespace and underscore).


``fill()`` -> int
   Add zeros to the end of the bitarray, such that the length of the bitarray
   will be a multiple of 8, and return the number of bits added (0..7).


``find(sub_bitarray, start=0, stop=<end of array>, /)`` -> int
   Return the lowest index where sub_bitarray is found, such that sub_bitarray
   is contained within ``[start:stop]``.
   Return -1 when sub_bitarray is not found.

   New in version 2.1.


``frombytes(bytes, /)``
   Extend the bitarray with raw bytes from a bytes-like object.
   Each added byte will add eight bits to the bitarray.

   New in version 2.5.0: allow bytes-like argument.


``fromfile(f, n=-1, /)``
   Extend bitarray with up to n bytes read from the file object f.
   When n is omitted or negative, reads all data until EOF.
   When n is provided and positive but exceeds the data available,
   ``EOFError`` is raised (but the available data is still read and appended.


``index(sub_bitarray, start=0, stop=<end of array>, /)`` -> int
   Return the lowest index where sub_bitarray is found, such that sub_bitarray
   is contained within ``[start:stop]``.
   Raises ``ValueError`` when the sub_bitarray is not present.


``insert(index, value, /)``
   Insert ``value`` into the bitarray before ``index``.


``invert(index=<all bits>, /)``
   Invert all bits in the array (in-place).
   When the optional ``index`` is given, only invert the single bit at index.

   New in version 1.5.3: optional index argument.


``iterdecode(code, /)`` -> iterator
   Given a prefix code (a dict mapping symbols to bitarrays, or ``decodetree``
   object), decode the content of the bitarray and return an iterator over
   the symbols.


``itersearch(sub_bitarray, /)`` -> iterator
   Searches for the given sub_bitarray in self, and return an iterator over
   the start positions where bitarray matches self.


``pack(bytes, /)``
   Extend the bitarray from a bytes-like object, where each byte corresponds
   to a single bit.  The byte ``b'\x00'`` maps to bit 0 and all other bytes
   map to bit 1.
   This method, as well as the unpack method, are meant for efficient
   transfer of data between bitarray objects to other python objects
   (for example NumPy's ndarray object) which have a different memory view.

   New in version 2.5.0: allow bytes-like argument.


``pop(index=-1, /)`` -> item
   Return the i-th (default last) element and delete it from the bitarray.
   Raises ``IndexError`` if bitarray is empty or index is out of range.


``remove(value, /)``
   Remove the first occurrence of ``value`` in the bitarray.
   Raises ``ValueError`` if item is not present.


``reverse()``
   Reverse all bits in the array (in-place).


``search(sub_bitarray, limit=<none>, /)`` -> list
   Searches for the given sub_bitarray in self, and return the list of start
   positions.
   The optional argument limits the number of search results to the integer
   specified.  By default, all search results are returned.


``setall(value, /)``
   Set all elements in the bitarray to ``value``.
   Note that ``a.setall(value)`` is equivalent to ``a[:] = value``.


``sort(reverse=False)``
   Sort the bits in the array (in-place).


``to01()`` -> str
   Return a string containing '0's and '1's, representing the bits in the
   bitarray.


``tobytes()`` -> bytes
   Return the bitarray buffer in bytes (unused bits are set to zero).


``tofile(f, /)``
   Write the byte representation of the bitarray to the file object f.


``tolist()`` -> list
   Return a list with the items (0 or 1) in the bitarray.
   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the bitarray object,
   which may cause a memory error if the bitarray is very large.


``unpack(zero=b'\x00', one=b'\x01')`` -> bytes
   Return bytes containing one character for each bit in the bitarray,
   using the specified mapping.


bitarray data descriptors:
--------------------------

``bitorder`` -> str
   bit order as a string -- same as .endian()


``buffer_obj`` -> bytes-like | None
   when buffer is imported, return the underlying object


``nbytes`` -> int
   buffer size in bytes


``readonly`` -> bool
   bool indicating whether buffer is read only


Other objects:
--------------

``frozenbitarray(initializer=0, /, endian='big', buffer=None)`` -> frozenbitarray
   Return a frozenbitarray object, which is initialized the same way a bitarray
   object is initialized.  A frozenbitarray is immutable and hashable.
   Its contents cannot be altered after it is created; however, it can be used
   as a dictionary key.

   New in version 1.1.


``decodetree(code, /)`` -> decodetree
   Given a prefix code (a dict mapping symbols to bitarrays),
   create a binary tree object to be passed to ``.decode()`` or ``.iterdecode()``.

   New in version 1.6.


Functions defined in the `bitarray` module:
-------------------------------------------

``bits2bytes(n, /)`` -> int
   Return the number of bytes necessary to store n bits.


``get_default_endian()`` -> string
   Return the default endianness for new bitarray objects being created.
   Unless ``_set_default_endian()`` is called, the return value is ``big``.

   New in version 1.3.


``test(verbosity=1, repeat=1)`` -> TextTestResult
   Run self-test, and return unittest.runner.TextTestResult object.


Functions defined in `bitarray.util` module:
--------------------------------------------

This sub-module was add in version 1.2.

``zeros(length, /, endian=None)`` -> bitarray
   Create a bitarray of length, with all values 0, and optional
   endianness, which may be 'big', 'little'.


``urandom(length, /, endian=None)`` -> bitarray
   Return a bitarray of ``length`` random bits (uses ``os.urandom``).

   New in version 1.7.


``pprint(bitarray, /, stream=None, group=8, indent=4, width=80)``
   Prints the formatted representation of object on ``stream`` (which defaults
   to ``sys.stdout``).  By default, elements are grouped in bytes (8 elements),
   and 8 bytes (64 elements) per line.
   Non-bitarray objects are printed by the standard library
   function ``pprint.pprint()``.

   New in version 1.8.


``make_endian(bitarray, /, endian)`` -> bitarray
   When the endianness of the given bitarray is different from ``endian``,
   return a new bitarray, with endianness ``endian`` and the same elements
   as the original bitarray.
   Otherwise (endianness is already ``endian``) the original bitarray is returned
   unchanged.

   New in version 1.3.


``rindex(bitarray, value=1, start=0, stop=<end of array>, /)`` -> int
   Return the rightmost (highest) index of ``value`` in bitarray.
   Raises ``ValueError`` if the value is not present.

   New in version 2.3.0: optional start and stop arguments.


``strip(bitarray, /, mode='right')`` -> bitarray
   Return a new bitarray with zeros stripped from left, right or both ends.
   Allowed values for mode are the strings: ``left``, ``right``, ``both``


``count_n(a, n, value=1, /)`` -> int
   Return lowest index ``i`` for which ``a[:i].count(value) == n``.
   Raises ``ValueError``, when n exceeds total count (``a.count(value)``).

   New in version 2.3.6: optional value argument.


``parity(a, /)`` -> int
   Return the parity of bitarray ``a``.
   This is equivalent to ``a.count() % 2`` (but more efficient).

   New in version 1.9.


``count_and(a, b, /)`` -> int
   Return ``(a & b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.


``count_or(a, b, /)`` -> int
   Return ``(a | b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.


``count_xor(a, b, /)`` -> int
   Return ``(a ^ b).count()`` in a memory efficient manner,
   as no intermediate bitarray object gets created.


``subset(a, b, /)`` -> bool
   Return ``True`` if bitarray ``a`` is a subset of bitarray ``b``.
   ``subset(a, b)`` is equivalent to ``(a & b).count() == a.count()`` but is more
   efficient since we can stop as soon as one mismatch is found, and no
   intermediate bitarray object gets created.


``ba2hex(bitarray, /)`` -> hexstr
   Return a string containing the hexadecimal representation of
   the bitarray (which has to be multiple of 4 in length).


``hex2ba(hexstr, /, endian=None)`` -> bitarray
   Bitarray of hexadecimal representation.  hexstr may contain any number
   (including odd numbers) of hex digits (upper or lower case).


``ba2base(n, bitarray, /)`` -> str
   Return a string containing the base ``n`` ASCII representation of
   the bitarray.  Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   The bitarray has to be multiple of length 1, 2, 3, 4, 5 or 6 respectively.
   For ``n=16`` (hexadecimal), ``ba2hex()`` will be much faster, as ``ba2base()``
   does not take advantage of byte level operations.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.9.


``base2ba(n, asciistr, /, endian=None)`` -> bitarray
   Bitarray of the base ``n`` ASCII representation.
   Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   For ``n=16`` (hexadecimal), ``hex2ba()`` will be much faster, as ``base2ba()``
   does not take advantage of byte level operations.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.9.


``ba2int(bitarray, /, signed=False)`` -> int
   Convert the given bitarray to an integer.
   The bit-endianness of the bitarray is respected.
   ``signed`` indicates whether two's complement is used to represent the integer.


``int2ba(int, /, length=None, endian=None, signed=False)`` -> bitarray
   Convert the given integer to a bitarray (with given endianness,
   and no leading (big-endian) / trailing (little-endian) zeros), unless
   the ``length`` of the bitarray is provided.  An ``OverflowError`` is raised
   if the integer is not representable with the given number of bits.
   ``signed`` determines whether two's complement is used to represent the integer,
   and requires ``length`` to be provided.


``serialize(bitarray, /)`` -> bytes
   Return a serialized representation of the bitarray, which may be passed to
   ``deserialize()``.  It efficiently represents the bitarray object (including
   its endianness) and is guaranteed not to change in future releases.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.8.


``deserialize(bytes, /)`` -> bitarray
   Return a bitarray given a bytes-like representation such as returned
   by ``serialize()``.

   See also: `Bitarray representations <https://github.com/ilanschnell/bitarray/blob/master/doc/represent.rst>`__

   New in version 1.8.

   New in version 2.5.0: allow bytes-like argument.


``vl_encode(bitarray, /)`` -> bytes
   Return variable length binary representation of bitarray.
   This representation is useful for efficiently storing small bitarray
   in a binary stream.  Use ``vl_decode()`` for decoding.

   See also: `Variable length bitarray format <https://github.com/ilanschnell/bitarray/blob/master/doc/variable_length.rst>`__

   New in version 2.2.


``vl_decode(stream, /, endian=None)`` -> bitarray
   Decode binary stream (an integer iterator, or bytes-like object), and return
   the decoded bitarray.  This function consumes only one bitarray and leaves
   the remaining stream untouched.  ``StopIteration`` is raised when no
   terminating byte is found.
   Use ``vl_encode()`` for encoding.

   See also: `Variable length bitarray format <https://github.com/ilanschnell/bitarray/blob/master/doc/variable_length.rst>`__

   New in version 2.2.


``huffman_code(dict, /, endian=None)`` -> dict
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the Huffman code, i.e. a dict mapping those symbols to
   bitarrays (with given endianness).  Note that the symbols are not limited
   to being strings.  Symbols may may be any hashable object (such as ``None``).


``canonical_huffman(dict, /)`` -> tuple
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the canonical Huffman code.  Returns a tuple containing:

   0. the canonical Huffman code as a dict mapping symbols to bitarrays
   1. a list containing the number of symbols of each code length
   2. a list of symbols in canonical order

   Note: the two lists may be used as input for ``canonical_decode()``.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/bitarray/blob/master/doc/canonical.rst>`__

   New in version 2.5.


``canonical_decode(bitarray, count, symbol, /)`` -> iterator
   Decode bitarray using canonical Huffman decoding tables
   where ``count`` is a sequence containing the number of symbols of each length
   and ``symbol`` is a sequence of symbols in canonical order.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/bitarray/blob/master/doc/canonical.rst>`__

   New in version 2.5.


