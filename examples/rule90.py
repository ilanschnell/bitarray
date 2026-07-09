import time
from bitarray.util import zeros


# https://en.wikipedia.org/wiki/Rule_90
# 1-D cellular automaton with periodic boundary conditions
def rule90(a):
    left = a.copy()
    right = a.copy()
    left.rotate(-1)
    right.rotate(1)
    return left ^ right


state = zeros(79)
state[39] = 1

while True:
    print(state.unpack(zero=b' ', one=b'#').decode())
    state = rule90(state)
    time.sleep(0.02)
