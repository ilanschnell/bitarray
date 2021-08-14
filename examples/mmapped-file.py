"""
Demonstrates how to memory map a file into a bitarray.
"""
from mmap import mmap, ACCESS_READ

from bitarray import bitarray


filename = 'big.data'
filesize = 10_000_000

# create a large file with zeros
with open(filename, 'wb') as fo:
    fo.write(filesize * b'\0')

# open the file in binary read-write mode for mapping into a bitarray
with open(filename, 'r+b') as f:
    mapping = mmap(f.fileno(), 0)
    a = bitarray(buffer=mapping, endian='little')

    assert len(a) == 8 * filesize
    assert not a.any()  # no bits 1
    a[-1] = 1           # set the last bit in the array to 1

# open in binary read-only mode
with open(filename, 'rb') as fi:
    m = mmap(fi.fileno(), 0, access=ACCESS_READ)
    b = bitarray(buffer=m, endian='little')

    assert len(b) == 8 * filesize
    assert b.count() == 1  # only one bit is set
    assert b[-1] == 1      # the last one
    #b[0] = 1   TypeError: cannot modify read-only memory
