import json
from base64 import standard_b64encode, standard_b64decode

from bitarray import bitarray
from bitarray.util import serialize, deserialize


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, bitarray):
            return {'bitarray': standard_b64encode(serialize(obj)).decode()}

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if isinstance(obj, dict) and len(obj) == 1 and obj.get('bitarray'):
            return deserialize(standard_b64decode(obj['bitarray']))

        return obj


def test():
    from random import randint
    from bitarray.util import urandom

    a = [urandom(n, endian=['little', 'big'][randint(0, 1)])
         for n in range(1000)]
    a.append({'key1': bitarray('010'),
              'key2': 'value2'})
    j = JSONEncoder().encode(a)
    print(j)
    b = JSONDecoder().decode(j)
    assert a == b
    for i in range(len(a)):
        if isinstance(a[i], bitarray):
            assert a[i] == b[i]
            assert a[i].endian() == b[i].endian()
    assert b[-1]['key1'] == bitarray('010')
    assert b[-1]['key2'] == 'value2'


if __name__ == '__main__':
    test()
