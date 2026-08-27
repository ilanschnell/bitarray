Comparison of bitarray codecs
=============================

The utility module ``bitarray.util`` provides three codecs for representing
bitarrays in self-delimiting binary streams:

* ``sc_encode()`` and ``sc_decode()`` use sparse encoding (SC).
* ``rl_encode()`` and ``rl_decode()`` use run-length encoding (RL).
* ``vl_encode()`` and ``vl_decode()`` use the variable-length format (VL).

These codecs serve different purposes.  In particular, encoding does not
necessarily make the representation smaller.  SC is designed to compress
sparse bitarrays, RL is effective when there are long uninterrupted runs, and
VL provides a small and predictable representation for short bitarrays.

Among these three codecs, SC should generally be considered the **default
choice**.  It benefits greatly from sparse regions, while its raw blocks keep
the overhead very small when the bitarray is densely populated.  RL and VL are
better choices only when their more specialized properties match the data or
the application.


Summary
-------

.. list-table::
   :header-rows: 1
   :widths: 8 20 36 36

   * - Codec
     - Best suited for
     - Strong points
     - Weak points
   * - SC
     - General large bitarrays, especially sparsely populated ones
     - Good default choice; very compact when only a few bits are 1; raw blocks
       keep the overhead very small in densely populated regions; handles
       mixed sparse and dense regions; preserves bit-endianness.
     - Favors sparse 1 bits rather than treating 0 and 1 symmetrically; has the
       most complex format and encoder; requires additional working memory
       while encoding.
   * - RL
     - Bitarrays containing long runs of identical bits
     - Very compact for long runs; treats runs of 0 and 1 symmetrically; simple
       format based on ULEB128 run lengths.
     - Can expand data substantially when runs are short; has no raw-block
       fallback; does not preserve bit-endianness.
   * - VL
     - Short bitarrays and streams containing many small bitarrays
     - Small fixed overhead; encoded size depends only on bitarray length;
       simple and fast; naturally self-terminating.
     - Does not take advantage of sparse bits or runs; uses approximately one
       byte per seven bits for long bitarrays; does not preserve
       bit-endianness.


Sparse encoding (SC)
--------------------

SC divides the bitarray into blocks and chooses between raw bytes and lists of
indices of 1 bits.  Index widths range from one to four bytes, allowing a very
large sparse region to be represented by a small number of indices.  Raw
blocks prevent highly populated regions from being stored as long index
lists.  Therefore, a bitarray containing both sparse and dense regions can be
represented efficiently.

The stream header records the bitarray length and bit-endianness.  Preserving
bit-endianness is necessary because raw blocks are copied directly between the
bitarray buffer and the encoded stream.  A stop byte terminates the encoded
object.

The encoded size depends on the distribution of 1 bits and the block choices
made by the encoder.  An all-zero bitarray is represented by only the header
and stop byte, regardless of its length.  For highly populated data, SC uses
raw blocks and adds only a small block-header overhead.

See `Compression of sparse bitarrays <sparse_compression.rst>`__ for the block
format, statistics, and examples.


Run-length encoding (RL)
------------------------

RL stores the first bit value, the bitarray length, and the lengths of
alternating runs.  The bitarray length and every run length use ULEB128.  Run
lengths are strictly positive and must add up exactly to the bitarray length,
so no stop value is needed.

If ``u(i)`` is the number of bytes in the ULEB128 representation of ``i``, an
RL stream containing runs of lengths ``r_1, ..., r_k`` uses

.. code-block:: text

    1 + u(nbits) + u(r_1) + ... + u(r_k)

bytes.  Thus, an all-zero or all-one bitarray needs only one encoded run.  At
the other extreme, an alternating bitarray has one run per bit and uses about
one encoded byte per bit.

The format describes logical bit values and does not contain
bit-endianness.  The optional ``endian`` argument to ``rl_decode()`` selects
the bit-endianness of the returned bitarray.


Variable-length format (VL)
---------------------------

VL stores four payload bits in the first byte and seven payload bits in each
additional byte.  The high bit of every byte indicates whether another byte
follows.  Three bits in the first byte record the amount of padding in the
final byte.  The format is similar to LEB128.

For a bitarray of length ``n``, the encoded size is

.. code-block:: python

    n // 7 + 1 + (n % 7 > 4)

bytes.  Consequently, an empty bitarray and every bitarray of up to four bits
fit in one byte.  VL does not inspect the bit values, so all bitarrays of the
same length have the same encoded size.  For long bitarrays, the representation
approaches one byte for every seven bits and is larger than the bitarray's raw
buffer.

Like RL, VL represents logical bits and does not store bit-endianness.  The
optional ``endian`` argument to ``vl_decode()`` selects the bit-endianness of
the returned bitarray.

See `Variable length bitarray format <variable_length.rst>`__ for a detailed
example.


Streaming behavior
------------------

Each decoder consumes exactly one encoded bitarray from an integer iterator
and leaves the remaining input untouched.  SC recognizes its stop byte, RL
stops when its positive run lengths add up to the stored bitarray length, and
VL stops at the first byte whose continuation bit is clear.  This makes all
three formats suitable for concatenating encoded objects in a stream.


Choosing a codec
----------------

Use SC as the default when choosing among these codecs.  It can substantially
reduce sparse data, adapts to mixed-density regions, and adds only minimal
overhead to dense data.  Use RL instead when long runs, rather than population,
characterize the data.  Use VL for short arbitrary bitarrays or when a simple,
predictable representation is more important than reducing size.

When none of the codec-specific properties is needed, ``serialize()`` may be
more appropriate: it stores the raw buffer together with the metadata needed
to reconstruct the bitarray.
