#
# Makefile for lab 7, part 2
#

CC = gcc

CFLAGS = -g -Wall $(INCLUDES)

http-server: http-server.o
	$(CC) -o http-server http-server.o

http-server.o: http-server.c

.PHONY: clean
clean:
	rm -f *.o *~ a.out core http-server

.PHONY: all
all: clean default


