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
    {'a': frozenbitarray('0'),
     'b': frozenbitarray('10'),
     'c': frozenbitarray('1110'),
     'd': frozenbitarray('1111'),
     'r': frozenbitarray('110')}
    >>> count
    [0, 1, 1, 1, 2]
    >>> symbol
    ['a', 'b', 'r', 'c', 'd']

The output is a tuple with the following elements:

* A dictionary mapping each symbol to a ``frozenbitarray``
* A list containing the number of symbols for each code length,
  e.g. ``count[3] = 1`` because there is one symbol (``r``) with
  code length ``3``.
* A list of symbols in canonical order

If we add up numbers in ``count``, we get the total number of symbols coded:

.. code-block:: python

   >>> sum(count) == len(symbol)
   True

In a canonical code, code words of the same length have consecutive values.
Therefore, the code can be reconstructed from the number of symbols of each
length and the symbols in canonical order, without storing the individual
code words.

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
    >>> a = bitarray()
    >>> a.encode(codedict, "abracadabra")
    >>> a
    bitarray('01011001110011110101100')
    >>> "".join(a.decode(codedict))
    'abracadabra'

We can also decode using the canonical decoding tables ``count`` and
``symbol``:

.. code-block:: python

    >>> from bitarray.util import canonical_decode
    >>> ''.join(canonical_decode(a, count, symbol))
    'abracadabra'

``canonical_decode()`` supports code lengths of up to 31 bits.  In contrast,
the regular ``.decode()`` method supports prefix codes with lengths of up to
256 bits.


Notes on DEFLATE
----------------

DEFLATE is a lossless compressed-data format that combines LZ77 and Huffman
coding.  It is used by the ``gzip`` and ``zlib`` formats and is organized into
blocks.  Each block either stores literal bytes without compression or uses
fixed or dynamically defined Huffman codes.

DEFLATE interleaves literal/length Huffman symbols, raw extra bits, and
distance Huffman symbols.  Because ``canonical_decode()`` expects one
uninterrupted stream encoded with a single canonical code, it is not well
suited to decoding DEFLATE blocks.  A more efficient implementation can be
found in `examples/puff <../examples/puff/>`__
