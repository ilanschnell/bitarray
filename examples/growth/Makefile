resize: resize.c
	gcc -Wall resize.c -o resize

test: resize
	./resize >pattern-c.txt
	python growth.py >pattern-py.txt
	diff pattern-c.txt pattern-py.txt
	python test.py

clean:
	rm -f resize
	rm -f pattern-*
