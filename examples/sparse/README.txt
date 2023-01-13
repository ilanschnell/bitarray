Ilan Schnell
January, 2023


Here are two different implementations of sparse bitarrays.

Makefile:
    Run:
    $ make test

common.py
    Common functionally used by both the flips and ones implementation.

flips.py
    The bitarray is represented by a list of positions at which a bit changes
    from 1 to 0 or vice versa.

ones.py:
    The bitarray is represented by a (sorted) list containing the position
    of 1 bits (as well as the length of the array).

tests.py
    Tests for both implementations
