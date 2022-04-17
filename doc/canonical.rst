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
