Variable-length prefix codes
============================

Bitarray provides efficient methods for encoding and decoding variable-length
prefix codes.  A prefix code maps symbols to bitarrays such that no code word
is a prefix of another.


Encoding and decoding
---------------------

The ``.encode()`` method takes a dictionary mapping symbols to bitarrays and
an iterable of symbols.  It extends the bitarray with the corresponding code
word for each symbol:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> code = {
    ...     'H': bitarray('111'),
    ...     'e': bitarray('0'),
    ...     'l': bitarray('110'),
    ...     'o': bitarray('10'),
    ... }
    >>> a = bitarray()
    >>> a.encode(code, 'Hello')
    >>> a
    bitarray('111011011010')

Symbols are not limited to characters; any hashable Python object may be a
symbol.  The string ``'Hello'`` is simply the iterable used in this example.

The ``.decode()`` method takes the same prefix code and returns an iterator
over the decoded symbols:

.. code-block:: python

    >>> "".join(a.decode(code))
    'Hello'


Constructing Huffman codes
--------------------------

The function ``bitarray.util.huffman_code()`` constructs a prefix code from
a frequency map:

.. code-block:: python

    >>> from bitarray.util import huffman_code
    >>> frequencies = {'a': 5, 'b': 2, 'c': 1}
    >>> code = huffman_code(frequencies)
    >>> message = 'abacaba'
    >>> a = bitarray()
    >>> a.encode(code, message)
    >>> ''.join(a.decode(code))
    'abacaba'

Bitarray also supports `canonical Huffman coding <canonical.rst>`__, which
represents a Huffman code using its code lengths and symbols in canonical
order.
For more background, see `Huffman coding in Python using bitarray
<http://ilan.schnell-web.net/prog/huffman/>`__.


Reusing decoding trees
----------------------

Before decoding, a code dictionary is converted internally into a decoding
tree.  For large codes used in many decode calls, repeatedly constructing the
same tree can take a significant amount of time.  A ``decodetree`` object
constructs it once and may be passed directly to ``.decode()``:

.. code-block:: python

    >>> from bitarray import decodetree
    >>> code = {'a': bitarray('0'), 'b': bitarray('1')}
    >>> tree = decodetree(code)
    >>> a = bitarray('0110')
    >>> list(a.decode(tree))
    ['a', 'b', 'b', 'a']

A ``decodetree`` object is immutable and can be reused for any number of
bitarrays.

See the `Huffman examples <../examples/huffman/>`__ for complete compression
programs and performance comparisons.
