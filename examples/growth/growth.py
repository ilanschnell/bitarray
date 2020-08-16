from bitarray import bitarray


def show(a):
    _ptr, size, _endian, _unused, alloc = a.buffer_info()
    print('%d  %d' % (size, alloc))

a = bitarray()
prev = -1
while len(a) < 2000:
    alloc = a.buffer_info()[4]
    if prev != alloc:
        show(a)
    prev = alloc
    a.append(1)

for i in 800_000, 400_000, 399_992, 0, 0, 80_000:
    if len(a) < i:
        a.extend(bitarray(i - len(a)))
    else:
        del a[i:]
    assert len(a) == i
    show(a)
