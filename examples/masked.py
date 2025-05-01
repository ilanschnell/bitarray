# example to illustrate masked indexing
from bitarray import bitarray


a = bitarray('1110000')
b = bitarray('1100110')
# select bits from a where b is 1
assert a[b] == bitarray('1100')

# set bits in a where b is 1
a[b] = bitarray('1010')
assert a == bitarray('1010100')

# delete bits in a where b is 1
del a[b]
assert a == bitarray('100')
print("Ok")
