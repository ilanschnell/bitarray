PYTHON?=python3
TEST_PYTHONS?=python2.7 python3.6 python3.7

bitarray/_bitarray.so: bitarray/_bitarray.c
	$(PYTHON) setup.py build_ext --inplace


test: bitarray/_bitarray.so
	$(PYTHON) -c "import bitarray; bitarray.test()"

test_allpythons:
	$(foreach PYTHON, $(TEST_PYTHONS), $(MAKE) clean; \
		$(MAKE) PYTHON=$(PYTHON) test;)


doc: bitarray/_bitarray.so
	$(PYTHON) update_readme.py


clean:
	rm -rf build dist
	rm -f bitarray/*.o bitarray/*.so
	rm -f bitarray/*.pyc
	rm -rf bitarray/__pycache__ *.egg-info
	rm -f README.html
