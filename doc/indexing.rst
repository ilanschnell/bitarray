Bitarray indexing
=================

Bitarrays can be indexed like usual Python lists.  They support slice
indexing and assignment:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> a = bitarray('01000001 01000010 01000011')
    >>> a[1::3]
    bitarray('10100001')
    >>> a[8:16:2] = bitarray('1111')
    >>> a
    bitarray('010000011110101001000011')
    >>> del a[::2]  # remove every second element
    >>> a
    bitarray('100110001001')


Arbitrary indexing
------------------

As of bitarray version 2.8, indices may also be list of arbitrary indices.
For example:

.. code-block:: python

    >>> a[[1, 2, 6, 7]] = 1  # set elements 1, 2, 5, 6, 7 to value 1
    >>> a
    bitarray('111110111001')
    >>> a[[-1, -2, 1, 0]]
    bitarray('1011')
    >>> del a[[0, 1, 5, 8, 9]]
    >>> a
    bitarray('1111101')
