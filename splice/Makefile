PREFIX=/usr/local/cdn
CC=gcc
CFLAGS=-D_GNU_SOURCE -std=c99 -Wall
LDFLAGS=-lpthread
SOURCES=spliced.c
EXECUTABLE=spliced
INCLUDE=-I/usr/local/include/
LINK=-L/usr/local/lib/

all:
	$(CC) $(INCLUDE) $(LINK) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $(EXECUTABLE)

install: 
	install -d $(PREFIX)/bin
	install -m 0755 $(EXECUTABLE) $(PREFIX)/bin
