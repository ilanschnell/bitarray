import json
from base64 import b64encode, b64decode

from bitarray import bitarray
from bitarray.util import serialize, deserialize


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, bitarray):
            return {'__bitarray__': b64encode(serialize(obj)).decode()}

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if set(obj) == {"__bitarray__"}:
            return deserialize(b64decode(obj["__bitarray__"]))

        return obj


def test():
    from bitarray.util import urandom

    a = [urandom(n * n, endian=['little', 'big'][n % 2]) for n in range(12)]
    a.append({'key1': bitarray('010'),
              'key2': 'value2',
              'key3': urandom(300)})
    j = JSONEncoder(indent=2).encode(a)
    print(j)

    b = JSONDecoder().decode(j)
    assert a == b
    for i in range(12):
        assert a[i].endian == b[i].endian


if __name__ == '__main__':
    test()
