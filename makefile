CC=gcc
CFLAGS=-Wall -g

fscheck: fscheck.c
	$(CC) -o fscheck $(CFLAGS) fscheck.c

clean:
	$(RM) fscheck
