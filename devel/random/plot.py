from collections import Counter
from statistics import fmean, pstdev

from matplotlib import pyplot as plt
from bitarray.util import random_k, sum_indices

from sample import SampleMeanDist

# The code will also work, if you change these parameters here:
SMD = SampleMeanDist(n=1_000, k=600)
M = 100_000  # number of trails
DX = 2.5  # width used for counting trail outcomes

def plot_pdf(plt, xmin, xmax):
    X = []
    Y = []
    n = 2_000
    for i in range(n + 1):
        x = xmin + i * (xmax - xmin) / n
        X.append(x)
        Y.append(SMD.pdf(x) * DX)
    plt.plot(X, Y)

def plot_count(plt, C):
    X = []
    Y = []
    for i in range(min(C), max(C) + 1):
        x = i * DX
        X.append(x)
        Y.append(C[i] / M)
    plt.scatter(X, Y, color='red')

if __name__ == '__main__':
    SMD.print()
    C = Counter()
    values = []
    for _ in range(M):
        x = sum_indices(random_k(SMD.n, SMD.k)) / SMD.k
        C[round(x / DX)] += 1
        values.append(x)
    assert C.total() == M
    print("mean", fmean(values))
    print("stdev", pstdev(values, SMD.mu))

    plot_count(plt, C)
    plot_pdf(plt, min(C) * DX, max(C) * DX)
    plt.show()
