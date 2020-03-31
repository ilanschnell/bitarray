# gene sequence example from @yoch, see
# https://github.com/ilanschnell/bitarray/pull/54

from random import choice
from timeit import timeit

from bitarray import bitarray


trans = {
    "A": bitarray("00"),
    "T": bitarray("01"),
    "G": bitarray("10"),
    "C": bitarray("11"),
}

N = 10000
seq = [choice("ATGC") for _ in range(N)]

arr = bitarray()
arr.encode(trans, seq)

assert arr.decode(trans) == seq

# decodage
t = timeit(lambda: arr.decode(trans), number=1000)
print(t)
