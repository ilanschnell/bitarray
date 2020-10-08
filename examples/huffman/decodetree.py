from random import random, randint
from time import time

from bitarray import bitarray, decodetree
from bitarray.util import huffman_code


N = 1_000_000


code = huffman_code({i: random() for i in range(N)})
print(len(code))

t0 = time()
tree = decodetree(code)
print('decodetree(code):  %9.6f sec' % (time() - t0))

print(tree.nodes())
plain = [randint(0, N - 1) for _ in range(100)]

a = bitarray()
a.encode(code, plain)

t0 = time()
res = a.decode(code)
print('decode(code):  %9.6f sec' % (time() - t0))
assert res == plain

t0 = time()
res = a.decode(tree)
print('decode(tree):  %9.6f sec' % (time() - t0))
assert res == plain
