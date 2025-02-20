# https://www.youtube.com/watch?v=Ks1pw1X22y4
from bitarray import bitarray

state = bitarray("1001")

for _ in range(20):
    print(state)
    newbit = state[-1] ^ state[-2]
    state >>= 1
    state[0] = newbit
