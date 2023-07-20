Bitarray indexing
=================

Bitarrays can be indexed like usual Python lists.  They support slice
indexing and assignment:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> a = bitarray('01000001 01000010 01000011')
    >>> a[1::3]
    bitarray('10100001')
    >>> a[8:20:2] = bitarray('110111')
    >>> a
    bitarray('010000011110001011100011')
    >>> del a[::2]  # remove every second element
    >>> a
    bitarray('100110001001')
    >>> a[::3] = 0  # set every third element to 0
    >>> a
    bitarray('000010001001')


Integer sequence indexing
-------------------------

As of bitarray version 2.8, indices may also be lists of arbitrary
indices (like in NumPy).  Negative values are permitted in the index list
and work as they do with single indices or slices.  For example:

.. code-block:: python

    >>> a = bitarray(12)
    >>> a.setall(0)
    >>> a[[1, 2, 5, 7]] = 1  # set elements 1, 2, 5, 7 to value 1
    >>> a
    bitarray('011001010000')
    >>> a[[-1, -2, 1, 0]]
    bitarray('0010')
    >>> del a[[0, 1, 5, 8, 9]]
    >>> a
    bitarray('1000100')
    >>> a[[1, 2, 4]] = bitarray('010')  # assign indices to elements
    >>> a
    bitarray('1010000')


Masked indexing
---------------

Also, as of bitarray version 2.8, indices may be bitarrays which are
considered masks.  For example:

.. code-block:: python

    >>> a =    bitarray('1001001')
    >>> mask = bitarray('1010111')
    >>> a[mask]  # create bitarray with items from `a` whos mask is 1
    bitarray('10001')
    >>> del a[mask]  # deletion items in `a` whos mask is 1
    >>> a
    bitarray('01')

Note that ``del a[mask]`` is equivalent to in-place version of selecting the
nverse mask ``a = a[~mask]``.

Also note that masked assignment is not implemented,
as ``a[mask] = 1`` would be equivalent to the bitwise operation ``a |= mask``.
And ``a[mask] = 0`` would be equivalent to ``a &= ~mask``.
