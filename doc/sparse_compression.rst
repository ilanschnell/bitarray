Compression of sparse bitarrays
===============================

In a ``bitarray`` object each byte in memory represents eight bits.
While this representation is very compact and efficient when dealing with
most data, there are situations when this representation is inefficient.
One such situation are sparsely populated bitarray.
That is, bitarray in which only a few bits are 1, but most bits are 0.
In this situation, one might consider using a data structure which stores
the indices of the 1 bits and not use the ``bitarray`` object at all.
However, having all of bitarray's functionality is very convenient.
It may be desired to convert ``bitarray`` objects into a more compact (index
based) format when storing objects on disk or sending them over the network.
This is the use case of the utility functions ``sc_encode()``
and ``sc_decode()``.
The lower the population count, the more efficient the compression will be:

.. code-block:: python

    >>> from bitarray import bitarray
    >>> from bitarray.util import zeros, sc_encode, sc_decode
    >>> a = zeros(1 << 24, 'little')  # 16 mbits
    >>> a[0xaa] = a[0xbbcc] = a[0xddeeff] = 1
    >>> blob = sc_encode(a)
    >>> blob
    b'\x04\x00\x00\x00\x01\xc3\x03\xaa\x00\x00\xcc\xbb\x00\xff\xee\xdd\x00'
    >>> assert sc_decode(blob) == a


How it works
------------

Consider a ``bitarray`` of length 256, that is 32 bytes of memory.
If we represent this object by the indices of 1 bits as one byte each,
the object will be represent more efficiently when the population (number
of 1 bits) is less than 32.  Based on the population, the
function ``sc_encode()`` chooses to represent the object as either raw bytes
or as bytes of indices of 1 bits.  These are the block types 0 and 1.

Next, we consider a ``bitarray`` of length 65536.  When each section of 256
bits has a population below 32, it would be stored as 256 blocks of type 1.
That is, we need 256 block headers and one (index) byte for each 1 bit.
However, when the total population is below 256, we could also introduce
a new block type 2 in which each index is represented by two bytes and
represent the entire bitarray as a single block (of type 2).
This saves us the 256 block headers (of type 1).
Similarly, with even less populated bitarrays, it will become more efficient
to move to blocks representing each index using 3 or more bytes.

The encoding algorithm starts at the front of the ``bitarray``, inspects
the population and decides which block type to use to encode the following
bits.  Once the first block is written, the algorithm moves on to inspecting
the remaining population, and so on.
This way, a large bitarray with densly and sparsely populated areas will
be compressed efficiently using different block types.

The binary blob consists of a header which encodes the bit-endianness and the
total length of the bitarray, i.e. the number of bits.  The header is followed
by an arbitrary number of blocks.  There are 5 block types.  Each block starts
with a block header encoding the block type and specifying the size of the
block data that follows.

.. code-block::

   block   head         count    count   bytes             block size
   type    byte                  byte    per index   (encoded)     (decoded)
   -------------------------------------------------------------------------
     0     0x00..0x9f  0..4096   no      raw         1..4097         0..4096
     1     0xa0..0xbf    0..31   no       1            1..32              32
     2     0xc2         0..255   yes      2           2..512           8,192
     3     0xc3         0..255   yes      3           2..767       2,097,152
     4     0xc4         0..255   yes      4          2..1022     536,870,912


As the decoder stops whenever the decoded block size is 0,
the head byte 0x00 (type 0 with no raw bytes) is considered the stop byte.


Speed
-----

We create a 64 mbit (8mb) random bitarray with a probability of 1/1024
for each bit being 1.  The table shows a comparison of different compression
methods:

.. code-block::

                     compress (ms)   decompress (ms)    ratio
   ----------------------------------------------------------
   serialize            3.876             1.002        1.0000
   sc                   5.502             2.703        0.0158
   gzip               918.937            10.057        0.0169
   bz2                 59.500            32.611        0.0117


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
   0.00002082   0.00057423         0         0      3914         1         0
   0.00003748   0.00084394         0         0      4085         1         0
   0.00006747   0.00132376         0         0      4096         0         0
   0.00012144   0.00218719         0         0      4096         0         0
   0.00021859   0.00374150         0         0      4096         0         0
   0.00039346   0.00653845         0         0      4096         0         0
   0.00070824   0.01157218         0         0      4096         0         0
   0.00127482   0.02062941         0         0      4096         0         0
   0.00229468   0.03691334         0         0      4096         0         0
   0.00413043   0.06410542         0    634074      1620         0         0
   0.00743477   0.09051070         0   1048576         0         0         0
   0.01338259   0.13759786         0   1048576         0         0         0
   0.02408866   0.22166717         0   1048576         0         0         0
   0.04335959   0.37071779         1   1048575         0         0         0
   0.07804726   0.65549386      6157   1042384         0         0         0
   0.14048506   0.98861155    176021    223348         0         0         0
   0.25287311   1.00024432      8192         0         0         0         0
   0.45517160   1.00024432      8192         0         0         0         0
   0.81930887   1.00024432      8192         0         0         0         0
