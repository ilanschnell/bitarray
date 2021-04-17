Reference
=========

bitarray version: 2.0.1 -- `change log <https://github.com/ilanschnell/bitarray/blob/master/doc/changelog.rst>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.


The bitarray object:
--------------------

``bitarray(initializer=0, /, endian='big')`` -> bitarray
   Return a new bitarray object whose items are bits initialized from
   the optional initial object, and endianness.
   The initializer may be of the following types:

   ``int``: Create a bitarray of given integer length.  The initial values are
   uninitialized.

   ``str``: Create bitarray from a string of ``0`` and ``1``.

   ``iterable``: Create bitarray from iterable or sequence or integers 0 or 1.

   The optional keyword arguments ``endian`` specifies the bit endianness of the
   created bitarray object.
   Allowed values are the strings ``big`` and ``little`` (default is ``big``).
   The bit endianness only effects the when buffer representation of the
   bitarray.


**A bitarray object supports the following methods:**

``all()`` -> bool
   Return True when all bits in the array are True.
   Note that ``a.all()`` is faster than ``all(a)``.


``any()`` -> bool
   Return True when any bit in the array is True.
   Note that ``a.any()`` is faster than ``any(a)``.


``append(item, /)``
   Append ``item`` to the end of the bitarray.


``buffer_info()`` -> tuple
   Return a tuple (address, size, endianness, unused, allocated) giving the
   memory address of the bitarray's buffer, the buffer size (in bytes),
   the bit endianness as a string, the number of unused bits within the last
   byte, and the allocated memory for the buffer (in bytes).


``bytereverse()``
   For all bytes representing the bitarray, reverse the bit order (in-place).
   Note: This method changes the actual machine values representing the
   bitarray; it does *not* change the endianness of the bitarray object.


``clear()``
   Remove all items from the bitarray.


``copy()`` -> bitarray
   Return a copy of the bitarray.


``count(value=1, start=0, stop=<end of array>, /)`` -> int
   Count the number of occurrences of ``value`` in the bitarray.


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
   Append all the items from ``iterable`` to the end of the bitarray.
   If the iterable is a string, each ``0`` and ``1`` are appended as
   bits (ignoring whitespace).


``fill()`` -> int
   Add zeros to the end of the bitarray, such that the length of the bitarray
   will be a multiple of 8, and return the number of bits added (0..7).


``frombytes(bytes, /)``
   Extend bitarray with raw bytes.  That is, each append byte will add eight
   bits to the bitarray.


``fromfile(f, n=-1, /)``
   Extend bitarray with up to n bytes read from the file object f.
   When n is omitted or negative, reads all data until EOF.
   When n is provided and positive but exceeds the data available,
   ``EOFError`` is raised (but the available data is still read and appended.


``index(value, start=0, stop=<end of array>, /)`` -> int
   Return index of the first occurrence of ``value`` in the bitarray.
   Raises ``ValueError`` if the value is not present.


``insert(index, value, /)``
   Insert ``value`` into the bitarray before ``index``.


``invert(index=<all bits>, /)``
   Invert all bits in the array (in-place).
   When the optional ``index`` is given, only invert the single bit at index.


``iterdecode(code, /)`` -> iterator
   Given a prefix code (a dict mapping symbols to bitarrays, or ``decodetree``
   object), decode the content of the bitarray and return an iterator over
   the symbols.


``itersearch(bitarray, /)`` -> iterator
   Searches for the given a bitarray in self, and return an iterator over
   the start positions where bitarray matches self.


``pack(bytes, /)``
   Extend the bitarray from bytes, where each byte corresponds to a single
   bit.  The byte ``b'\x00'`` maps to bit 0 and all other characters map to
   bit 1.
   This method, as well as the unpack method, are meant for efficient
   transfer of data between bitarray objects to other python objects
   (for example NumPy's ndarray object) which have a different memory view.


``pop(index=-1, /)`` -> item
   Return the i-th (default last) element and delete it from the bitarray.
   Raises ``IndexError`` if bitarray is empty or index is out of range.


``remove(value, /)``
   Remove the first occurrence of ``value`` in the bitarray.
   Raises ``ValueError`` if item is not present.


``reverse()``
   Reverse the order of bits in the array (in-place).


``search(bitarray, limit=<none>, /)`` -> list
   Searches for the given bitarray in self, and return the list of start
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
   bitarray object.


``tobytes()`` -> bytes
   Return the byte representation of the bitarray.
   When the length of the bitarray is not a multiple of 8, the few remaining
   bits are considered 0.


``tofile(f, /)``
   Write the byte representation of the bitarray to the file object f.
   When the length of the bitarray is not a multiple of 8, the few remaining
   bits are considered 0.


``tolist()`` -> list
   Return a list with the items (0 or 1) in the bitarray.
   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the bitarray object,
   which may cause a memory error if the bitarray is very large.


``unpack(zero=b'\x00', one=b'\x01')`` -> bytes
   Return bytes containing one character for each bit in the bitarray,
   using the specified mapping.


Other objects:
--------------

``frozenbitarray(initializer=0, /, endian='big')`` -> frozenbitarray
   Return a frozenbitarray object, which is initialized the same way a bitarray
   object is initialized.  A frozenbitarray is immutable and hashable.
   Its contents cannot be altered after it is created; however, it can be used
   as a dictionary key.


``decodetree(code, /)`` -> decodetree
   Given a prefix code (a dict mapping symbols to bitarrays),
   create a binary tree object to be passed to ``.decode()`` or ``.iterdecode()``.


Functions defined in the `bitarray` module:
-------------------------------------------

``bits2bytes(n, /)`` -> int
   Return the number of bytes necessary to store n bits.


``get_default_endian()`` -> string
   Return the default endianness for new bitarray objects being created.
   Under normal circumstances, the return value is ``big``.


``test(verbosity=1, repeat=1)`` -> TextTestResult
   Run self-test, and return unittest.runner.TextTestResult object.


Functions defined in `bitarray.util` module:
--------------------------------------------

``zeros(length, /, endian=None)`` -> bitarray
   Create a bitarray of length, with all values 0, and optional
   endianness, which may be 'big', 'little'.


``urandom(length, /, endian=None)`` -> bitarray
   Return a bitarray of ``length`` random bits (uses ``os.urandom``).


``pprint(bitarray, /, stream=None, group=8, indent=4, width=80)``
   Prints the formatted representation of object on ``stream``, followed by a
   newline.  If ``stream`` is ``None``, ``sys.stdout`` is used.  By default, elements
   are grouped in bytes (8 elements), and 8 bytes (64 elements) per line.
   Non-bitarray objects are printed by the standard library
   function ``pprint.pprint()``.


``make_endian(bitarray, endian, /)`` -> bitarray
   When the endianness of the given bitarray is different from ``endian``,
   return a new bitarray, with endianness ``endian`` and the same elements
   as the original bitarray.
   Otherwise (endianness is already ``endian``) the original bitarray is returned
   unchanged.


``rindex(bitarray, value=1, /)`` -> int
   Return the rightmost index of ``value`` in bitarray.
   Raises ``ValueError`` if the value is not present.


``strip(bitarray, mode='right', /)`` -> bitarray
   Return a new bitarray with zeros stripped from left, right or both ends.
   Allowed values for mode are the strings: ``left``, ``right``, ``both``


``count_n(a, n, /)`` -> int
   Return the smallest index ``i`` for which ``a[:i].count() == n``.
   Raises ``ValueError``, when n exceeds total count (``a.count()``).


``parity(a, /)`` -> int
   Return the parity of bitarray ``a``.
   This is equivalent to ``a.count() % 2`` (but more efficient).


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


``base2ba(n, asciistr, /, endian=None)`` -> bitarray
   Bitarray of the base ``n`` ASCII representation.
   Allowed values for ``n`` are 2, 4, 8, 16 and 32.
   For ``n=16`` (hexadecimal), ``hex2ba()`` will be much faster, as ``base2ba()``
   does not take advantage of byte level operations.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.


``ba2int(bitarray, /, signed=False)`` -> int
   Convert the given bitarray into an integer.
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


``deserialize(bytes, /)`` -> bitarray
   Return a bitarray given the bytes representation returned by ``serialize()``.


``huffman_code(dict, /, endian=None)`` -> dict
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the Huffman code, i.e. a dict mapping those symbols to
   bitarrays (with given endianness).  Note that the symbols are not limited
   to being strings.  Symbols may may be any hashable object (such as ``None``).


