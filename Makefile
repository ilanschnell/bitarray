bitarray/_bitarray.so: bitarray/_bitarray.c
	$(PYTHON) setup.py build_ext --inplace


test: bitarray/_bitarray.so
	$(PYTHON) -c "import bitarray; bitarray.test()"


doc: bitarray/_bitarray.so
	$(PYTHON) update_readme.py


clean:
	rm -rf build dist
	rm -f bitarray/*.o bitarray/*.so
	rm -f bitarray/*.pyc
	rm -f examples/*.pyc
	rm -rf bitarray/__pycache__ *.egg-info
	rm -rf examples/__pycache__
