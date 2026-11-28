CC = gcc
CFLAGS = -Wall -Wextra -Wno-implicit-function-declaration -Wunused-function -Wreturn-type

all: workload.c iolib.c banner.c
	$(CC) $(CFLAGS) workload.c iolib.c banner.c -o final
	$(CC) -static $(CFLAGS) workload.c iolib.c banner.c -o final_static

static: workload.c iolib.c banner.c
	$(CC) -static $(CFLAGS) workload.c iolib.c banner.c -o final_static

dynamic: workload.c iolib.c banner.c
	$(CC) $(CFLAGS) workload.c iolib.c banner.c -o final

clean:
	rm -f final final_static

.PHONY: all static dynamic clean