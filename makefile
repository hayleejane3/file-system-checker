CC=gcc
CFLAGS=-Wall -g

all:
	$(CC) -o fscheck $(CFLAGS) fscheck.c

clean:
	$(RM) fscheck
