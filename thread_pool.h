/*
 * thread_pool.h
 */

void tp_init(int);
 int tp_create(int);
void tp_activate(void(*)(void*),void*);
void tp_suspend();
void tp_done();

int current_thread;

#define tp_read(filedes,buf,nbytes) _tp_aio(fildes,buf,nbytes,1)
#define tp_write(filedes,buf,nbytes) _tp_aio(filedes,buf,nbytes,0)

int tp_send(int, char*, size_t, int);
int tp_recv(int, char*, size_t, int);
