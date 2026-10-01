from bitarray import bitarray
from bitarray.util import ba2int, pprint
from bitarray.bitfields import unpack

# See: https://en.wikipedia.org/wiki/UTF-16

#         high surrogate       low surrogate
FORMAT = "110110xx xxxxxxxx    110111xx xxxxxxxx"
FIELDS = ">p{110110} u10{high} p{110111} u10{low}"


def code_point(u):
    print("character:", u)
    b = u.encode("utf-16-be")
    print("hexadecimal:", " ".join("%02x" % i for i in b))
    a = bitarray(b, endian="big")
    pprint(a)

    assert a.nbytes in (2, 4)

    if a.nbytes == 2:
        code_point, = unpack(">u16", a)
    else:
        fields = unpack(FIELDS, a)
        # Each surrogate contributes 10 bits to the supplementary offset.
        code_point = 0x10000 + (fields.high << 10) + fields.low

        high = 0xd800 + fields.high
        low  = 0xdc00 + fields.low
        print("surrogates: %04x %04x" % (high, low))
        assert unpack(">u16 u16", a) == (high, low)
        mask = bitarray(FORMAT.replace("1", "0").replace("x", "1"))
        assert 0x10000 + ba2int(a[mask]) == code_point

    print("code point:", hex(code_point))
    print()
    assert code_point == ord(u)


for u in "\u0024 \u00a2 \u20ac \ud55c \U00010348 \U0001f603 \U0010ffff".split():
    code_point(u)
