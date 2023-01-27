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
    >>> blob = sc_encode(a)
    >>> blob
    b'\x02\x00\x04\xc0\x02\x85\x01\x0c\x03\x00'
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
