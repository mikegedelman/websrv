#include <stdlib.h>
#include <unistd.h>
#include <aio.h>
#include <errno.h>

#include "mctx.h"
#include "thread_pool.h"

typedef struct thread {
   mctx_t *ctx;
   //unsigned int id;
   void* arg;
   int priority;
   void (*job)(void*);
   void* sk_addr;
} thread_t;

thread_t **threads;
int *thread_priority;

static int num_threads = -1;
static int max_threads = -1;
//static int current_thread = -1;

static int sigstksz = 16384;

static mctx_t save_mctx;

void tp_init(int max_thr) {
    current_thread = -1;
    max_threads = max_thr;
    threads = malloc(max_threads*sizeof(thread_t));
    thread_priority = malloc(max_threads*sizeof(int));
    num_threads = 0;
}

void tp_function(void* sf_arg) {
   while(1) {
      //printf("thread %i activated\n", current_thread);
      //print_stack();
      (threads[current_thread]->job)(threads[current_thread]->arg); 
      tp_done();

      //printf("hit end of thread_fxn\n");
   }
}


/* return -1 for error */
int tp_create(int num) {
   int i;
   for (i=0;i<num;i++) {
      if (num_threads == max_threads)
         return -1;

      num_threads++;
      thread_t *thr = malloc(sizeof(thread_t));
      thr->ctx = malloc(sizeof(mctx_t));
      thr->priority = -1;
      thr->arg = NULL;
      thr->job = NULL;
      thr->sk_addr = malloc(sigstksz);
      threads[num_threads-1] = thr;

      mctx_create(thr->ctx, tp_function, NULL, thr->sk_addr, sigstksz);
   }

   return 0;
}

/* get an inactive thread's id or return -1 if they're all busy */
static int _tp_get_inactive() {
   int i;
   for (i=0;i<num_threads;i++)
      if (threads[i]->priority == -1)
         return i;

   return -1;
}

/* return id of highest priority or -1 if all inactive */
static int _tp_get_highest_priority() {
   int i;
   int min = -1;
   int lowest;
   for (i=0;i<num_threads;i++)
      if (threads[i]->priority >= 0) {
         min = threads[i]->priority;
         lowest = i;
         break;
      }

   if (min==-1)
      return -1;

   for (i=0;i<num_threads;i++)
      if (threads[i]->priority >= 0)
         if (threads[i]->priority < min) {
            min = threads[i]->priority;
            lowest = i;
         }

   return lowest;
}

void tp_suspend() {
   int cur_thr = current_thread;
   current_thread = -1;
   mctx_switch(threads[cur_thr]->ctx, &save_mctx);

   return;
}

/* wake up an inactive worker thread, passing it 'arg'
 * blocks until a thread is actually activated 
 */
void tp_activate(void (*fxn)(void*), void* arg) {
   //printf("called thread_activate on %x arg %i\n", fxn, *(int*)arg);
   int id;
   while ((id = _tp_get_inactive()) < 0)
      tp_continue();

   if (id < 0) {
      puts("fatal error");
      exit(0);
   }

   threads[id]->arg = arg;
   threads[id]->priority = _tp_get_highest_priority()+1;
   threads[id]->job = fxn;
   current_thread = id;
   mctx_switch(&save_mctx, threads[id]->ctx);

   return;
}



int _tp_aio(int fildes, char *buf, int nbytes, int read_flag) {
   struct aiocb aio;
   struct sigevent sig;
   sig.sigev_notify = SIGEV_NONE;
   aio.aio_fildes = fildes;
   aio.aio_offset = 0;
   aio.aio_buf = buf;
   aio.aio_nbytes = nbytes;
   aio.aio_reqprio = 0;
   aio.aio_sigevent = sig;
   
   if (aio_write(&aio) < 0) {
      perror("aio_write");
      return -1;
   }

   const struct aiocb* aiocb_list[1];
   aiocb_list[0] = &aio;

   tp_suspend();
   aio_suspend(aiocb_list, 1, NULL);

   return 0;
}


void tp_done() {
   threads[current_thread]->priority = -1;
   int cur_thr = current_thread;
   current_thread = -1;
   mctx_switch(threads[cur_thr]->ctx, &save_mctx); 

   return;
}

/* acts a scheduler. picks the highest priority active thread and continues
 * execution. returns -1 if no active threads; otherwise 1.
 */
int tp_continue() {
   int next_id = _tp_get_highest_priority();
   if (next_id < 0)
      return -1;

   current_thread = next_id;
   mctx_switch(&save_mctx, threads[current_thread]->ctx);
   return 1;
}

/* call this before exit */
void tp_cleanup() {
   int i;
   for (i=0;i<num_threads;i++) {
      free(threads[i]->ctx);
      free(threads[i]);
   }
   max_threads = -1;
   num_threads = -1;
}
