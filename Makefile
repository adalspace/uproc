main: main.c
	gcc -o main -I../uipc/include/ -L./ -ggdb -g -Wall -Wextra main.c -l:libuipc.a
