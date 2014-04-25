all: websrv-proc

websrv-proc: websrv-proc.c
	gcc -w -g -o websrv-proc websrv-proc.c

websrv-thread: websrv-thread.c
	gcc -w -g -o websrv-thread websrv-thread.c thread_pool.c mctx.c -lm -lrt

