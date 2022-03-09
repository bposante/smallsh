setup:
	gcc -std=gnu99 -g -Wall -o smallsh smallsh.c

clean:
	rm smallsh
	rm test*.txt
	