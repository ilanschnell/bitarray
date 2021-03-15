import json
import binascii

from bitarray import bitarray


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, bitarray):
            return {
                'type': 'bitarray',
                'bytes': binascii.hexlify(obj.tobytes()).decode(),
                'len': len(obj),
                'endian': obj.endian()
            }

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if isinstance(obj, dict) and obj.get('type') == 'bitarray':
            a = bitarray(endian=obj['endian'])
            a.frombytes(binascii.unhexlify(obj['bytes'])),
            del a[obj['len']:]
            return a

        return obj


def test():
    from random import randint
    from bitarray.util import urandom

    a = [urandom(n, endian=['little', 'big'][randint(0, 1)])
         for n in range(1000)]
    a.append({'key1': bitarray('010'),
              'key2': 'value2'})
    j = JSONEncoder().encode(a)
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
