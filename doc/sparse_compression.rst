Compression of sparse bitarrays
===============================

The two utility functions ``sc_encode()`` and ``sc_decode()`` provide
functionality to efficiently compress and decompress sparse bitarrays.
The lower the population count, the more efficient the compression will be:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> from bitarray.util import zeros, sc_encode, sc_decode
    >>> a = zeros(1024, 'little')
    >>> a[389] = a[780] = 1
    >>> data = sc_encode(a)
    >>> data
    b'L\x00\x00\x01\x85\x00\x81\x0c'
    >>> assert sc_decode(data) == a
