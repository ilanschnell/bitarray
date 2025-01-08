from bitarray import bitarray


def get_alloc(a):
    return a.buffer_info()[4]

def show(a):
    info = a.buffer_info()
    print('%18d %10d %10d' % (info[0], info[1], info[4]))


# make sure sequence of appends will always increase allocated size
a = bitarray()
prev = -1
while len(a) < 1_000_000:
    alloc = get_alloc(a)
    assert prev <= alloc
    prev = alloc
    a.append(1)


# ensure that when we start from a large array and delete part, we always
# get a decreasing allocation
a = bitarray(10_000_000)
prev = get_alloc(a)
for _ in range(100):
    del a[-100_000:]
    alloc = a.buffer_info()[4]
    assert alloc <= prev
    prev = alloc


# initalizing a bitarray from a list or bitarray should not overallocate
for n in range(1000):
    alloc = (n + 3) & ~3
    assert n <= alloc < n + 4 and alloc % 4 == 0
    a = bitarray(8 * n * [1])
    assert alloc == get_alloc(a)
    b = bitarray(a)
    assert alloc == get_alloc(b)


# starting from a large bitarray, make we sure we don't realloc each time
# we extend
a = bitarray(1_000_000)  # no overallocation
assert get_alloc(a) == 125_000
a.extend(bitarray(8))  # overallocation happens here
alloc = get_alloc(a)
for _ in range(1000):
    a.extend(bitarray(8))
    assert get_alloc(a) == alloc


print("OK")
