from bitarray import bitarray

from test_resize import get_alloc, resize, show


s = 290797
def bbs():
    global s
    s = pow(s, 2, 50515093)
    return s % 8000

a = bitarray()
prev = -1
while len(a) < 1_000:
    alloc = get_alloc(a)
    if prev != alloc:
        show(a)
    prev = alloc
    a.append(1)

for i in 800_000, 400_000, 399_992, 500_000, 0, 0, 10_000, 400, 600, 2_000:
    resize(a, i)
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

for _ in range(100_000):
    resize(a, bbs())
    show(a)
