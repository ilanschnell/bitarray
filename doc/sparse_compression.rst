Compression of sparse bitarrays
===============================

The two utility functions ``sc_encode()`` and ``sc_decode()`` provide
functionality to efficiently compress and decompress sparse bitarrays.
The lower the population count, the more efficient the compression will be:

.. code-block:: python
    >>> from bitarray import bitarray
    >>> from bitarray.util import zeros, sc_encode, sc_decode
    >>> a = zeros(1 << 30, 'little')  # 2^30 bits
    >>> a[123] = a[4_567] = a[890_123_456] = 1
    >>> blob = sc_encode(a)
    >>> blob
    b'\x04\x00\x00\x00@\xc2\x03{\x00\x00\x00\xd7\x11\x00\x00\xc04\x0e5\x00'
    >>> assert sc_decode(blob) == a


Strategy
--------

Our bitarray if divided into blocks ...


Speed
-----

...


Statistics
----------

...


Binary compression format
-------------------------

...
