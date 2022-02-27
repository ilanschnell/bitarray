from bitarray import bitarray


def show(a):
    info = a.buffer_info()
    print('%18d %10d %10d' % (info[0], info[1], info[4]))


# make sure sequence of appends will always increase allocated size
a = bitarray()
prev = -1
while len(a) < 1_000_000:
    alloc = a.buffer_info()[4]
    assert prev <= alloc
    prev = alloc
    a.append(1)


# ensure that when we start from a large array and delete part, we always
# get a decreasing allocation
a = bitarray(10_000_000)
prev = a.buffer_info()[4]
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
    assert alloc == a.buffer_info()[4]
    b = bitarray(a)
    assert alloc == b.buffer_info()[4]


# starting from a large bitarray, make we sure we don't realloc each time
# we extend
a = bitarray(1_000_000)  # no overallocation
assert a.buffer_info()[4] == 125_000
a.extend(bitarray(8))  # overallocation happens here
alloc = a.buffer_info()[4]
for _ in range(1000):
    a.extend(bitarray(8))
    assert a.buffer_info()[4] == alloc


print("OK")
