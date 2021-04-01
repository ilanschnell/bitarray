from random import random, randint
from time import time

from bitarray import bitarray, decodetree
from bitarray.util import huffman_code


N = 1000 * 1000

# create Huffman code for N symbols
code = huffman_code({i: random() for i in range(N)})
print(len(code))

# create the decodetree object
t0 = time()
tree = decodetree(code)
print('decodetree(code):  %9.6f sec' % (time() - t0))

print(tree.nodes())
plain = [randint(0, N - 1) for _ in range(100)]

a = bitarray()
a.encode(code, plain)

# decode using the code dictionary
t0 = time()
res = a.decode(code)
print('decode(code):  %9.6f sec' % (time() - t0))
assert res == plain

# decode using the decodetree
t0 = time()
res = a.decode(tree)
print('decode(tree):  %9.6f sec' % (time() - t0))
assert res == plain
assert tree.todict() == code
