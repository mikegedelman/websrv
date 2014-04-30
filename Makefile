all: websrv-thread

websrv-proc: websrv-proc.c 
	gcc -w -g -o websrv-proc websrv-proc.c

websrv-thread: websrv-thread.c thread_pool.c mctx.c
	gcc -w -g -o websrv websrv-thread.c thread_pool.c mctx.c -lm -lrt

