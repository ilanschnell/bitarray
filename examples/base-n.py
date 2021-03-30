from bitarray.util import urandom, pprint, ba2base, base2ba


a = urandom(60)

for m in range(1, 7):
    n = 1 << m
    print("----- length: %d ----- base: %d ----- " % (m, n))
    pprint(a, group=m)
    rep = ba2base(n, a)
    print("representation:", rep)
    print()
    assert base2ba(n, rep) == a
