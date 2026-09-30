from bitarray import bitarray
from bitarray.util import ba2int, pprint
from bitarray.bitfields import unpack

# See: https://en.wikipedia.org/wiki/UTF-8

FORMAT = {
    1: "0xxxxxxx",
    2: "110xxxxx 10xxxxxx",
    3: "1110xxxx 10xxxxxx 10xxxxxx",
    4: "11110xxx 10xxxxxx 10xxxxxx 10xxxxxx"
}

def code_point(u):
    print("character:", u)
    b = u.encode("utf-8")
    print("hexadecimal:", " ".join("%02x" % i for i in b))
    a = bitarray(b, endian="big")
    pprint(a)

    fmt = FORMAT[a.nbytes]
    fields = fmt.replace("0", "p").replace("1", "P").replace("x", "?")
    payload = bitarray(unpack(fields, a), endian="big")
    code_point = ba2int(payload)
    print("code point:", hex(code_point))
    print()

    assert code_point == ord(u)

    # The payload can be extracted more efficiently using a mask, but this
    # does not validate the fixed prefix bits.
    mask = bitarray(fmt.replace("1", "0").replace("x", "1"))
    assert a[mask] == payload
    # Validate prefix bits using inverse mask.
    assert a[~mask] == bitarray(fmt.replace("x", ""))


for u in "\u0024 \u00a2 \u20ac \ud55c \U00010348 \U0001f603 \U0010ffff".split():
    code_point(u)
