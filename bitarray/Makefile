# This Makefile is only used as an aid for developing.

#all:
#	make clean
#	make _bitarray.so
#	make test

PYTHON=python2.7


_bitarray.so: _bitarray.c
#	gcc -W -Wall -Wextra -shared -fPIC -I/usr/local/include/python2.5 -o _bitarray.so _bitarray.c
	cd .. && ${PYTHON} setup.py build_ext --inplace


clean:
	rm _bitarray.so
	rm -rf ../build


test:
	${PYTHON} test_bitarray.py
