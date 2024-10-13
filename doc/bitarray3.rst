Bitarray 3 transition
=====================

The bitarray version 3 release is bitarray's farewell to Python 2.
Apart from removing Python 2 support, this release also migrates
bitarray's ``.decode()`` and ``.search()`` methods to return iterators.
This is similar to how Python's ``dict.keys()``, ``.values()``
and ``.items()`` methods were revamped in the Python 2 to 3 transition.

In the following table, ``a`` is assumed to a bitarray object.

+----------------------+----------------------+
| before version 3     | version 3            |
+======================+======================+
| ``a.iterdecode()``   | ``a.decode()``       |
+----------------------+----------------------+
| ``a.decode()``       | ``list(a.decode()``  |
+----------------------+----------------------+
| ``a.itersearch()``   | ``a.search()``       |
+----------------------+----------------------+
| ``a.search()``       | ``list(a.search()``  |
+----------------------+----------------------+
