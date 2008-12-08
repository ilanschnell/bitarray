# This Makefile is only used as an aid for developing.

#all:
#	make clean
#	make bitarraymodule.so
#	make test


_bitarray.so: _bitarray.c
#	gcc -Wall -shared -fPIC -I/usr/local/include/python2.5 -o _bitarray.so _bitarray.c
	cd .. && python setup.py build_ext --inplace


clean:
	rm _bitarray.so
	rm -rf ../build


test:
	python test_bitarray.py


cleanup:
	expand _bitarray.c | sed -e 's:[ ]*$$::' >tmp.c
	mv tmp.c _bitarray.c
