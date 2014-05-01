all: websrv

websrv: websrv.c thread_pool.c mctx.c
	gcc -w -g -o websrv websrv.c thread_pool.c mctx.c -lm -lrt

