Canonical Huffman Coding
========================

Bitarray supports creating, encoding and decoding canonical Huffman codes.
Consider the following frequency map:

.. code-block:: python

    >>> cnt = {'a': 5, 'b': 3, 'c': 1, 'd': 1, 'r': 2}

We can now use ``canonical_huffman()`` to create a canonical Huffman code:

.. code-block:: python

    >>> from pprint import pprint
    >>> from bitarray.util import canonical_huffman
    >>> codedict, count, symbol = canonical_huffman(cnt)
    >>> pprint(codedict)
    {'a': bitarray('0'),
     'b': bitarray('10'),
     'c': bitarray('1110'),
     'd': bitarray('1111'),
     'r': bitarray('110')}
    >>> count
    [0, 1, 1, 1, 2]
    >>> symbol
    ['a', 'b', 'r', 'c', 'd']

The output is tuple with the following elements:

* A dictionary mapping each symbols to a ``bitarray``
* A list containing the number of symbols for each code length,
  e.g. `count[3] = 1` because there is one symbol (``r``) with
  code length ``3``.
* A list of symbols in canonical order

If we add up numbers in ``count``, we get the total number of symbols coded:

.. code-block:: python

   >>> sum(count) == len(symbol)
   True

The canonical Huffman code is:

.. code-block::

    index  symbol  code  length
    ---------------------------
      0      a     0       1
      1      b     10      2
      2      r     110     3
      3      c     1110    4
      4      d     1111    4

Encode a message using this code:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> msg = "abracadabra"
    >>> a = bitarray()
    >>> a.encode(codedict, msg)
    >>> a
    bitarray('01011001110011110101100')
    >>> assert ''.join(a.iterdecode(codedict)) == msg

And now decode using not ``codedict``, but the canonical decoding
tables ``count`` and ``symbol`` instead:

.. code-block:: python

    >>> from bitarray.util import canonical_decode
    >>> ''.join(canonical_decode(a, count, symbol))
    'abracadabra'


Side note on DEFLATE:
---------------------

DEFLATE is a lossless data compression file format that uses a combination
of LZ77 and Huffman coding.  It is used by ``gzip`` and implemented
in ``zlib``.  The format is organized in blocks, which contain Huffman
encoded data (except for raw blocks).  In addition to symbols that represent
bytes, there is a stop symbol and up to 29 LZ77 match length symbols.
When a LZ77 symbol is encountered, more bits are read from the stream
before continuing with decoding the next element in the stream.
The fact that extra bits are taken from the stream makes our
decode function (``canonical_decode()``) unsuitable for DEFLATE,
or at least inefficient as we would have to create a new iterator for
decoding each symbol.
