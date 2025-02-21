# Linear Feedback Shift Register
# https://www.youtube.com/watch?v=Ks1pw1X22y4
# https://en.wikipedia.org/wiki/Linear-feedback_shift_register
import itertools

from bitarray import bitarray
from bitarray.util import parity

state = 0b1001
for _ in range(20):
    print(state & 1, end='')
    newbit = (state ^ (state >> 1)) & 1
    state = (state >> 1) | (newbit << 3)
print()

def test_tabs(tabs, verbose=False):
    n = len(tabs)
    state0 = bitarray(n)
    state0[0] = state0[-1] = 1
    state = state0.copy()
    for i in itertools.count(1):
        if verbose:
            print(state[0], end='')
        newbit = parity(state & tabs)
        state <<= 1
        state[-1] = newbit
        if state == state0:
            break
    if verbose:
        print()
    return i

test_tabs(bitarray("1100"), True)

for tabs in [
        "11",
        "110",
        "1100",
        "10100",
        "110000",
        "1100000",
        "10111000",
        "100010000",
        "1001000000",
        "10100000000",
        "111000001000",
        "1110010000000",
        "11100000000010",
        "110000000000000",
        "1101000000001000",
        "10010000000000000",
        "100000010000000000",
        "1110010000000000000",
        "10010000000000000000",
        "101000000000000000000",
        "1100000000000000000000",
        "10000100000000000000000",
        "111000010000000000000000",
]:
    period = test_tabs(bitarray(tabs))
    print(period)
    n = len(tabs)
    assert period == 2 ** n - 1
