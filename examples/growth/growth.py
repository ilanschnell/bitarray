from bitarray import bitarray


prev = -1


def show(a):
    _pts, size, _endian, _unused, alloc = a.buffer_info()
    print('%d  %d' % (size, alloc))


a = bitarray()
while len(a) < 8 * 200:
    alloc = a.buffer_info()[4]
    if prev != alloc:
        show(a)
    prev = alloc
    a.append(1)

a.extend(bitarray(8 * 100_000 - len(a)))
show(a)
for i in 50_000, 49_999, 0, 0:
    del a[8 * i :]
    show(a)
a.extend(bitarray(8 * 10_000))
show(a)
