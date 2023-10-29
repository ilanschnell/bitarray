from random import random, randrange
from time import perf_counter

from bitarray import bitarray, decodetree
from bitarray.util import huffman_code


N = 100_000

# create Huffman code for N symbols
code = huffman_code({i: random() for i in range(N)})
print(len(code))

# create the decodetree object
t0 = perf_counter()
tree = decodetree(code)
print('decodetree(code):  %9.6f ms' % (1000.0 * (perf_counter() - t0)))

print(tree.nodes())
plain = [randrange(N) for _ in range(100)]

a = bitarray()
a.encode(code, plain)

# decode using the code dictionary
t0 = perf_counter()
res = a.decode(code)
print('decode(code):  %9.6f ms' % (1000.0 * (perf_counter() - t0)))
assert res == plain

# decode using the decodetree
t0 = perf_counter()
res = a.decode(tree)
print('decode(tree):  %9.6f ms' % (1000.0 * (perf_counter() - t0)))
assert res == plain
assert tree.todict() == code
