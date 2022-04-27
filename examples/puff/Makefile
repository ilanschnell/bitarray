PYTHON=python

_puff.so: _puff.c
	$(PYTHON) setup.py build_ext --inplace

test: _puff.so
	$(PYTHON) test_puff.py

clean:
	rm -rf *.o *.so __pycache__/ build/
