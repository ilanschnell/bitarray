"""
see: https://www.youtube.com/watch?v=umTbivyJoiI
"""
import sys
from math import log2
from collections import Counter

from bitarray.util import huffman_code


def efficiency(freq, code):
    total = sum(freq.values())  # total frequency
    H = 0.0                     # entropy
    L = 0.0                     # average length
    for s in freq.keys():
        p = freq[s] / total     # probability
        H -= p * log2(p)
        L += p * len(code[s])
    print('H =', H)
    print('L =', L)
    return H / L                # efficiency

if len(sys.argv) > 1:
    with open(sys.argv[1], 'rb') as fi:
        plain = fi.read()
else:
    plain = b'aaabbbcde'

freq = Counter(plain)
code = huffman_code(freq)
print("Efficiency =", efficiency(freq, code))
