from bitarray import bitarray

from test_resize import get_alloc, show


a = bitarray()
prev = -1
while len(a) < 19_000_000:
    alloc = get_alloc(a)
    if prev != alloc:
        show(a)
    prev = alloc
    a.extend(bitarray(8 * 65536))

a = bitarray()
prev = -1
while len(a) < 1_000:
    alloc = get_alloc(a)
    if prev != alloc:
        show(a)
    prev = alloc
    a.append(1)

for i in 800_000, 400_000, 399_992, 0, 0, 80_000, 2_000:
    if len(a) < i:
        a.extend(bitarray(i - len(a)))
    else:
        del a[i:]
    assert len(a) == i
    show(a)

while len(a):
    alloc = get_alloc(a)
    if prev != alloc:
        show(a)
    prev = alloc
    a.pop()

show(a)

for nbits in range(0, 100, 8):
    a = bitarray()
    a.extend(bitarray(nbits))
    show(a)
