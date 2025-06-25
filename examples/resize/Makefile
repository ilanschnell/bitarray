resize: resize.c
	gcc -Wall resize.c -o resize

test: resize
	./resize >pattern-c.txt
	python resize.py >pattern-py.txt
	diff pattern-c.txt pattern-py.txt
	python test_resize.py

clean:
	rm -f resize
	rm -f pattern-*
	rm -rf __pycache__
