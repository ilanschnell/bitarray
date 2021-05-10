#
# This example illusatrates how binary data can be efficiently be passed
# between a bitarray object and an ndarray with dtype bool
#
from __future__ import print_function

import bitarray
import numpy

a = bitarray.bitarray('100011001001')
print(a)

# bitarray  ->  ndarray
b = numpy.frombuffer(a.unpack(), dtype=bool)
print(repr(b))

# ndarray  ->  bitarray
c = bitarray.bitarray()
c.pack(b.tobytes())

assert a == c
