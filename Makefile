
bitarray/_bitarray.so: bitarray/_bitarray.c
	python setup.py build_ext --inplace


test: bitarray/_bitarray.so
	python -c "import bitarray; bitarray.test()"


doc: bitarray/_bitarray.so
	python update_readme.py


clean:
	rm -rf build dist
	rm -f bitarray/*.o bitarray/*.so
	rm -f bitarray/*.pyc
	rm -rf bitarray/__pycache__ *.egg-info
	rm -f README.html
