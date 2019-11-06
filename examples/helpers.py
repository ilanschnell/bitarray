from bitarray import bitarray


def count_n(a, n):
    "return the index i for which a[:i].count() == n"
    i, j = n, a.count(1, 0, n)
    while j < n:
        if a[i]:
            j += 1
        i += 1
    return i

if __name__ == '__main__':
    # count_n
    a = bitarray('11111011111011111011111001111011111011111011111010111010111')
    for n in range(0, 48):
        i = count_n(a, n)
        assert a[:i].count() == n
