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
   int block_type;
   int time;
   int timeout;
} thread_t;

thread_t **threads;

static int num_threads = -1;
static int max_threads = -1;
//static int current_thread = -1;

static int sigstksz = 16384;

static mctx_t save_mctx;

void tp_init(int max_thr) {
    current_thread = -1;
    max_threads = max_thr;
    threads = malloc(max_threads*sizeof(thread_t));
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
      thr->time = 0;
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

/*
 * 0 if suspending read, 1 if write
 */
int tp_suspend(int i, int timeout) {
   threads[current_thread]->block_type = i;
   threads[current_thread]->timeout = timeout;
   threads[current_thread]->time   = 0;
   int cur_thr = current_thread;
   current_thread = -1;
   mctx_switch(threads[cur_thr]->ctx, &save_mctx);

   if (threads[current_thread]->time > threads[current_thread]->timeout)
      return -1;
   else
      return 0;
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


/*
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

   tp_suspend(0);
   aio_suspend(aiocb_list, 1, NULL);

   return 0;
}*/


void tp_done() {
   threads[current_thread]->priority = -1;
   threads[current_thread]->arg = NULL;
   threads[current_thread]->job = NULL;
   int cur_thr = current_thread;
   current_thread = -1;
   mctx_switch(threads[cur_thr]->ctx, &save_mctx); 

   return;
}

/* acts a scheduler. picks the highest priority active thread and continues
 * execution. returns -1 if no active threads; otherwise 1.
 *
int tp_continue() {
   int next_id = _tp_get_highest_priority();
   if (next_id < 0)
      return -1;

   current_thread = next_id;
   mctx_switch(&save_mctx, threads[current_thread]->ctx);
   return 1;
}*/



int tp_continue() {
   int i;
   int max = 0;

   /* see if any threads are active */
   int highest_priority = _tp_get_highest_priority();
   if (highest_priority < 0)
      return -1;

   fd_set rfds;
   fd_set wfds;
   FD_ZERO(&rfds);
   FD_ZERO(&wfds);

   struct timeval tv;
   tv.tv_sec = 1;
   tv.tv_usec = 0;
      
   for (i=0; i<max_threads;i++)
      if (threads[i]->priority >= 0 && threads[i]->arg != NULL) {
         int fd = *(int*)threads[i]->arg;
         if (fd>max)
            max = fd;

         if (threads[i]->block_type == 0) 
            FD_SET(fd, &rfds);
         else if (threads[i]->block_type == 1) 
            FD_SET(fd, &wfds);
         else {
            puts("fatal error");
            exit(0);
         }
         threads[i]->time++;
      }

   int retval = select(max+1, &rfds, &wfds, NULL, &tv);

   if (retval < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
         perror("select");
         exit(0);
      }
   }
   else if (retval == 0) {
         for (i=0;i<max_threads;i++)
            if (threads[i]->priority >= 0)
               if(threads[i]->time > threads[i]->timeout) {
                  current_thread = i;
                  mctx_switch(&save_mctx, threads[i]->ctx);
                  current_thread = -1;
               }
         return 1;
   }
   
   
   int switch_thread = -1;
   for (i=0; i<max_threads;i++)
      if (threads[i]->priority >= 0 && threads[i]->arg != NULL) {
         int fd = *(int*)threads[i]->arg;

         if (FD_ISSET(fd,&rfds)) {
            switch_thread = i;
            break;
         }
         else if (FD_ISSET(fd,&wfds)) {
            switch_thread = i;
            break;
         }

      }

   if (switch_thread < 0) {
      puts("error");
      return 1;
   }
   else {
      current_thread = switch_thread;
      threads[i]->time = 0;
      mctx_switch(&save_mctx, threads[current_thread]->ctx);
      return 1;
   }
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

int tp_recv(int sockfd, char *buf, size_t len, int flags, int recv_timeout) {
   int bytes_recv = 0;
   int bytes;

   while (bytes_recv < 1) {
      bytes = recv(sockfd, buf+bytes_recv, len, flags);

      if (bytes < 0) {
         if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("tp_recv");
            return -1;
         }

         if (tp_suspend(0, recv_timeout) < 0)
            return -1;
      }
      else if (bytes == 0) /* client closed connection */
         return -1;
      
      else
         bytes_recv += bytes;
   }

   return bytes_recv;
}

int tp_send(int sockfd, char *buf, size_t nbytes, int flags) {
   int bytes_sent = 0;
   int bytes;

   while (bytes_sent < nbytes) {
      bytes = send(sockfd, buf+bytes_sent, nbytes, flags);

      if (bytes < 0) {
         if (errno != EAGAIN || errno != EWOULDBLOCK) {
            perror("tp_send");
            return -1;
         }
         tp_suspend(1, 100);
      }
      else
         bytes_sent += bytes;
   }
   return bytes_sent;
}
