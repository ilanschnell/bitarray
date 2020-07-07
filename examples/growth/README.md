The bitarray growth pattern
===========================

Running `python growth.py` will display the bitarray growth pattern.
This is done by appending one bit to a bitarray in a loop, and displaying
the allocated size of the bitarray object each time it changes.

The program `resize.c` contains a distilled version of the `resize()`
function which contains the implementation of this growth pattern.
Running this C program gives exactly the same output.
