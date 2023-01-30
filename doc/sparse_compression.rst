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


We binary blob consists of a header which encodes the bit endianness and the
total length of the bitarray, i.e. the number of bits.  The header is followed
by an arbitrary number of blocks and a final stop byte.  There are 5 block
types.  Each block starts with a block header encoding the block type and
specifying the size of the block data that follows.

+--------+------------+--------+-------+-----------+-----------+-------------+
| block  | head       | count  | count | bytes     |   size    |     size    |
| type   | byte       |        | byte  | per index | (encoded) |   (decoded) |
+========+============+========+=======+===========+===========+=============+
| stop   | 0x00       |        |       |           |        1  |             |
| type 0 | 0x01..0x80 | 1..128 | no    | raw       |   2..129  |      1..128 |
| type 1 | 0xa0..0xbf |  0..31 | no    |  1        |    1..32  |          32 |
| type 2 | 0xc2       | 0..255 | yes   |  2        |   2..512  |       8,192 |
| type 3 | 0xc3       | 0..255 | yes   |  3        |   2..767  |   2,097,152 |
| type 4 | 0xc4       | 0..255 | yes   |  4        |  2..1022  | 536,870,912 |
+--------+------------+--------+-------+-----------+-----------+-------------+



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

We create 256 mbit (32mb) random bitarrays with varying probability ``p``
for elements being 1.  After compression, we look at the compression
ratio, and the number of blocks of each type:

.. code-block::

        p          ratio         raw    type 1    type 2    type 3    type 4
   -------------------------------------------------------------------------
   0.00000001   0.00000048         0         0         0         0         1
   0.00000002   0.00000072         0         0         0         0         1
   0.00000003   0.00000119         0         0         0         0         1
   0.00000006   0.00000203         0         0         0         0         1
   0.00000010   0.00000358         0         0         0         0         1
   0.00000019   0.00000560         0         0         0        16         0
   0.00000034   0.00000927         0         0         0        16         0
   0.00000061   0.00001580         0         0         0        16         0
   0.00000110   0.00002751         0         0         0        16         0
   0.00000198   0.00004870         0         0         0        16         0
   0.00000357   0.00008678         0         0         0        16         0
   0.00000643   0.00015536         0         0         0        16         0
   0.00001157   0.00027874         0         0         0        16         0
   0.00002082   0.00057420         0         0      3913         1         0
   0.00003748   0.00084397         0         0      4080         1         0
   0.00006747   0.00132376         0         0      4096         0         0
   0.00012144   0.00218725         0         0      4096         0         0
   0.00021859   0.00374156         0         0      4096         0         0
   0.00039346   0.00653821         0         0      4096         0         0
   0.00070824   0.01157171         0         0      4096         0         0
   0.00127482   0.02062804         0         0      4096         0         0
   0.00229468   0.03691709         0         0      4096         0         0
   0.00413043   0.06410414         0    631808      1628         0         0
   0.00743477   0.09050894         0   1048576         0         0         0
   0.01338259   0.13760078         0   1048576         0         0         0
   0.02408866   0.22164792         0   1048576         0         0         0
   0.04335959   0.37073088         0   1048576         0         0         0
   0.07804726   0.65535405      5951   1042579         0         0         0
   0.14048506   0.99192983    285433    223548         0         0         0
   0.25287311   1.00781268    262144         0         0         0         0
   0.45517160   1.00781268    262144         0         0         0         0
   0.81930887   1.00781268    262144         0         0         0         0


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
