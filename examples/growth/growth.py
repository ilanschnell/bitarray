from bitarray import bitarray

from test_resize import get_alloc, show


def resize(a, n):
    if len(a) < n:
        a.extend(bitarray(n - len(a)))
    else:
        del a[n:]

def bbs(s=290797):
    while True:
        s = pow(s, 2, 50515093)
        yield s % 1000

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

t = bbs()
for _ in range(100_000):
    resize(a, 8 * next(t))
    show(a)
