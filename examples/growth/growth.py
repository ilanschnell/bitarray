from bitarray import bitarray


prev = -1


def show(a):
    ptr, size, _endian, _unused, alloc = a.buffer_info()
    print('%18d %10d %10d' % (ptr, size, alloc))

a = bitarray()
while len(a) < 8 * 200:
    alloc = a.buffer_info()[4]
    if prev != alloc:
        show(a)
    prev = alloc
    a.append(1)

a.extend(bitarray(8 * 100_000 - len(a)))
show(a)
a.extend(bitarray(8 * 10))
show(a)

for i in range(100, 0, -8): # 50_000, 49_999, 1, 0:
    del a[8 * i :]
    show(a)
a.extend(bitarray(8 * 10_000))
show(a)

for _ in range(2):
    a.clear()
    show(a)

b = bitarray(8 * 10_000_000)
show(b)
for _ in range(10):
    del b[-8 * 1000_000:]
    show(b)

for n in 60_000, 80_000:
    b = bitarray(8 * n * '1')
    show(b)

a = bitarray()
prev = -1
while len(a) < 1_000_000:
    alloc = a.buffer_info()[4]
    assert prev <= alloc
    prev = alloc
    a.append(1)
print('---')
a = bitarray(80_000)
show(a)
for _ in range(10):
    a.extend(bitarray(8))
    show(a)
