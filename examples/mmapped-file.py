"""
Demonstrates how to memory map a file into a bitarray.
"""
import os
import mmap

from bitarray import bitarray


filename = 'big.data'
filesize = 10_000_000

# create a large file with zeros
with open(filename, 'wb') as fo:
    fo.write(filesize * b'\0')

# open file in binary read-write mode for mapping into bitarray
with open(filename, 'r+b') as f:
    mapping = mmap.mmap(f.fileno(), 0)
    a = bitarray(buffer=mapping, endian='little')

    assert len(a) == 8 * filesize
    assert not a.any()  # no bits 1
    a[-1] = 1           # set the last bit in the array to 1

# open in binary read-only mode
with open(filename, 'rb') as fi:
    m = mmap.mmap(fi.fileno(), 0, access=mmap.ACCESS_READ)
    b = bitarray(buffer=m, endian='little')

    assert len(b) == 8 * filesize
    assert b.count() == 1  # only one bit is set
    assert b[-1] == 1      # the last one
    try:
        b[0] = 1           # TypeError: cannot modify read-only memory
    except TypeError:
        pass
    assert b[0] == 0       # wasn't changed, still 0


os.unlink(filename)
print("OK")
