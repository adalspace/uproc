CC=gcc
AR=ar
CFLAGS=-Wall -Wextra -ggdb -g -I./include/
LIBS=-L./lib/ -l:libuipc.a

.PHONY: all
all: build build/uproc.o lib/libuproc.a lib/libuproc.so build/server

build:
	mkdir -p build/ lib/

build/uproc.o: src/uproc.c include/uproc/uproc.h
	$(CC) -c -fPIC -o build/uproc.o $(CFLAGS) src/uproc.c $(LIBS)

build/server: src/main.c lib/libuproc.a
	$(CC) -o build/server $(CFLAGS) src/main.c $(LIBS) -l:libuproc.a

lib/libuproc.a: build/uproc.o
	$(AR) rcs lib/libuproc.a build/uproc.o

lib/libuproc.so: build/uproc.o
	$(CC) -shared -o lib/libuproc.so build/uproc.o

clean:
	rm -r build/ lib/libuproc*

