import json
from base64 import b64encode, b64decode

from bitarray import bitarray
from bitarray.util import serialize, deserialize


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, bitarray):
            if len(obj) > 50:
                return {'bitarray_b64': b64encode(serialize(obj)).decode()}
            else:
                return {'bitarray': obj.to01()}

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if isinstance(obj, dict) and len(obj) == 1:
            if 'bitarray_b64' in obj:
                return deserialize(b64decode(obj['bitarray_b64']))

            if 'bitarray' in obj:
                return bitarray(obj['bitarray'])

        return obj


def test():
    from random import getrandbits
    from bitarray.util import urandom

    a = [urandom(n * n, endian=['little', 'big'][getrandbits(1)])
         for n in range(12)]
    a.append({'key1': bitarray('010'),
              'key2': 'value2',
              'key3': urandom(300)})
    j = JSONEncoder(indent=2).encode(a)
    print(j)

    b = JSONDecoder().decode(j)
    assert a == b
    assert b[-1]['key1'] == bitarray('010')


if __name__ == '__main__':
    test()
