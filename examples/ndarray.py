# This example illustrates how binary data can be passed efficiently
# between a bitarray object and an ndarray with dtype bool.

from bitarray import bitarray
import numpy as np

a = bitarray('10001100 11110001 10011')
print(a)

# bitarray.unpack() expands each bit into one byte.  frombuffer() views those
# bytes as Boolean values without making another copy.
b = np.frombuffer(a.unpack(), dtype=bool)
print(b)

# Conversely, ndarray.tobytes() stores each Boolean in one byte, and
# bitarray.pack() converts each byte back to one bit.
c = bitarray()
c.pack(b.tobytes())

assert a == c

# NumPy can also pack Boolean values eight per byte.  The final byte is padded
# with zeros because the array length is not a multiple of eight.
packed = np.packbits(b)
print(packed)
assert bytes(packed) == bytes(a)

# count discards the padding bits when unpacking the NumPy array.
assert np.array_equal(b, np.unpackbits(packed, count=len(a)))
