PYTHON=python


bitarray/_bitarray.so: bitarray/_bitarray.c
	$(PYTHON) setup.py build_ext --inplace


test: bitarray/_bitarray.so
	$(PYTHON) setup.py test


install:
	$(PYTHON) -m pip install -vv .


doc: bitarray/_bitarray.so
	$(PYTHON) update_doc.py
	$(PYTHON) setup.py sdist
	twine check dist/*


mypy:
	mypy bitarray/*.pyi
	mypy bitarray/test_*.py
	mypy examples/*.py
	mypy examples/huffman/*.py
	mypy examples/sparse/*.py


clean:
	rm -rf build dist
	rm -f bitarray/*.o bitarray/*.so
	rm -f bitarray/*.pyc
	rm -f examples/*.pyc
	rm -rf bitarray/__pycache__ *.egg-info
	rm -rf examples/__pycache__ examples/*/__pycache__
	rm -rf .mypy_cache bitarray/.mypy_cache
	rm -rf examples/.mypy_cache examples/*/.mypy_cache
