Ilan Schnell
April, 2022


This is a simple implemention of inflate based on Mark Adler's excellent Puff:
https://github.com/madler/zlib/blob/master/contrib/puff/puff.c

While writing this, I also found useful:
https://github.com/nayuki/Simple-DEFLATE-decompressor/tree/master/python


I wrote this to better understand the DEFLATE format, and also to give
an example on how to write a Python C extension which makes use of bitarray
on the C-level.


To try it out (you need to have bitarray installed into your Python 3):

    $ make test
    ...
    $ python gunzip.py <file.gz> <output>
    ...


Files:

_puff.c
  - an object State (similar to the struct state in puff.c)

puff.py
  - a class Puff which inherits from State

gunzip.py
  - a class GunZip which inherits from Puff
  - a simple CLI
