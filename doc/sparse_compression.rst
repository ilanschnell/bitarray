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
    b'\x04\x00\x00\x00@\xc4\x03{\x00\x00\x00\xd7\x11\x00\x00\xc04\x0e5\x00'
    >>> assert sc_decode(blob) == a


Strategy
--------

Our bitarray if divided into blocks ...


Speed
-----

We create a 64 mbit (8mb) random bitarray with a probability of 1/1024
for each bit being 1.  The table shows a comparison of different compression
methods:

.. code-block::

                     compress (ms)   decompress (ms)    ratio
   ----------------------------------------------------------
   serialize            4.562             1.188        1.0000
   sc                  28.117             2.620        0.0158
   gzip               920.343            16.161        0.0169
   bz2                 59.580            33.435        0.0117


Statistics
----------

...


Binary compression format
-------------------------

.. code-block::

   block    head         count    count   bytes             block size
   type     byte                  byte    per index   (encoded)     (decoded)
   --------------------------------------------------------------------------
   stop     0x00                                            1
   type 0   0x01..0x80   1..128   no      raw          2..129          1..128
   type 1   0xa0..0xbf    0..31   no       1            1..32              32
   type 2   0xc2         0..255   yes      2           2..512           8,192
   type 3   0xc3         0..255   yes      3           2..767       2,097,152
   type 4   0xc4         0..255   yes      4          2..1022     536,870,912
