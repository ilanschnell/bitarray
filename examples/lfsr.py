# Linear Feedback Shift Register
# https://www.youtube.com/watch?v=Ks1pw1X22y4
from bitarray import bitarray
from bitarray.util import parity

state = 0b1001
for _ in range(20):
    print(state & 1, end='')
    newbit = (state ^ (state >> 1)) & 1
    state = (state >> 1) | (newbit << 3)
print()

state = bitarray("1001")
tabs =  bitarray("1100")
for _ in range(20):
    print(state[0], end='')
    newbit = parity(state & tabs)
    state <<= 1
    state[-1] = newbit
print()
