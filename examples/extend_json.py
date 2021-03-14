import json

from bitarray import bitarray


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, bitarray):
            return {'bitarray': obj.to01(),
                    'endian': obj.endian()}

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if (isinstance(obj, dict) and len(obj) == 2 and
            'bitarray' in obj and 'endian' in obj):
            return bitarray(obj['bitarray'], endian=obj['endian'])

        return obj


a = {'abc': bitarray('110'), 'def': [12, 34, 56]}
#a = bitarray('001')
print(a)
j = JSONEncoder().encode(a)
print(j)
b = JSONDecoder().decode(j)
assert a == b
