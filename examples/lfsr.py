# Linear Feedback Shift Register
# https://www.youtube.com/watch?v=Ks1pw1X22y4
# https://en.wikipedia.org/wiki/Linear-feedback_shift_register
from bitarray import bitarray
from bitarray.util import parity


def get_period(tabs, verbose=False):
    "given a bitarray of tabs return period of lfsr"
    n = len(tabs)
    state0 = bitarray(n)
    state0[0] = state0[-1] = 1
    state = state0.copy()
    period = 0
    while True:
        if verbose:
            print(state[0], end='')
        newbit = parity(state & tabs)
        state <<= 1
        state[-1] = newbit
        period += 1
        if state == state0:
            break
    if verbose:
        print()
    return period

def simple():
    "example from computerphile"
    state = 0b1001
    for _ in range(20):
        print(state & 1, end='')
        newbit = (state ^ (state >> 1)) & 1
        state = (state >> 1) | (newbit << 3)
    print()
    get_period(bitarray("1100"), True)

def test_wiki():
    "test list of tabs shown on Wikipedia"
    all_tabs = [
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
    ]
    for tabs in all_tabs:
        tabs = bitarray(tabs)
        period = get_period(tabs)
        print(period)
        n = len(tabs)
        assert period == 2 ** n - 1
        assert parity(tabs) == 0

def n128():
    tabs = bitarray(128)
    tabs[[0, 1, 2, 7]] = 1

    state = bitarray(128)
    state[0] = state[-1] = 1

    while True:
        print(state[0], end='')
        newbit = parity(state & tabs)
        state <<= 1
        state[-1] = newbit

if __name__ == "__main__":
    simple()
    test_wiki()
    #n128()
