CFLAGS = -Wno-implicit-function-declaration -wall -wextra

all: workload.c iolib.c banner.c
	gcc $(CFLAGS) workload.c iolib.c banner.c -o final
	gcc -static $(CFLAGS) workload.c iolib.c banner.c -o final_static
	
static: main.c banner.c banner.h 
	gcc -static $(CFLAGS) workload.c iolib.c banner.c -o final_static

dynamic: main.c banner.c banner.h
	gcc $(CFLAGS) workload.c iolib.c banner.c -o final

clean:
	rm -f final final_static