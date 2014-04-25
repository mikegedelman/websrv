/*static mctx_t mctx_caller;
static sig_atomic_t mctx_called;
static mctx_t *mctx_creat;
static void (*mctx_creat_func)( void *);
static void *mctx_creat_arg;
static sigset_t mctx_creat_sigs;*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mctx.h"

static mctx_t mctx_caller;
static sig_atomic_t mctx_called;
static mctx_t *mctx_creat;
static void (*mctx_creat_func)( void *);
static void *mctx_creat_arg;

static sigset_t mctx_creat_sigs;

void mctx_create_trampoline(int);

/*void mctx_switch(mctx_t *old_ctx, mctx_t *new_ctx) {
   printf("mctx_switch to %x\n", new_ctx->jb->__jmpbuf+2);
   if (setjmp(old_ctx->jb) == 0)
      longjmp(new_ctx->jb, 1);
}*/

void mctx_create( 
   mctx_t *mctx, 
   void (*sf_addr)( void *), void *sf_arg, 
   void *sk_addr, size_t sk_size) 
{
   struct sigaction sa;
   struct sigaction osa;
   //struct sigaltstack ss;
   //struct sigaltstack oss;

   stack_t ss;
   stack_t oss;

   sigset_t osigs;
   sigset_t sigs;
   
   /* Step 1: */ 
   sigemptyset(&sigs);
   sigaddset(&sigs, SIGUSR1);
   sigprocmask(SIG_BLOCK, &sigs, &osigs);

   /* Step 2: */
   memset(( void *)&sa, 0, sizeof ( struct sigaction));
   sa.sa_handler = mctx_create_trampoline;
   sa.sa_flags = SA_ONSTACK;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGUSR1, &sa, &osa) != 0)
      return;

   /* Step 3: */ 
   ss.ss_sp = sk_addr;
   ss.ss_size = sk_size;
   ss.ss_flags = 0;
   if (sigaltstack(&ss, &oss) < 0) {
      perror("sigaltstack");
      return;
   }

   /* Step 4: */ 
   mctx_creat = mctx;
   mctx_creat_func = sf_addr;
   mctx_creat_arg = sf_arg;
   mctx_creat_sigs = osigs;
   mctx_called = FALSE;
   kill(getpid(), SIGUSR1);
   sigfillset(&sigs);
   sigdelset(&sigs, SIGUSR1);
   while (!mctx_called) 
      sigsuspend(&sigs);
     
   /* Step 6: */ 
   sigaltstack(NULL, &ss);
   ss.ss_flags = SS_DISABLE;
   sigaltstack(&ss, NULL);
   if (!(oss.ss_flags & SS_DISABLE)) 
      sigaltstack(&oss, NULL);
   sigaction(SIGUSR1, &osa, NULL);
   sigprocmask(SIG_SETMASK, &osigs, NULL);
      
   /* Step 7 & Step 8: */ 
   mctx_switch(&mctx_caller, mctx);

   /* Step 14: */
   return;
}

   
void mctx_create_trampoline(int sig)
{
   /* Step 5:*/ 
   if (mctx_save(mctx_creat) == 0) {
      mctx_called = TRUE;
      return ;
   }

   /*mctx_save(mctx_creat);
   mctx_called = TRUE;
   return;*/

   /* Step 9: */ 
   mctx_create_boot();
}

void mctx_create_boot(void)
{
   void (*mctx_start_func)( void *);
   void *mctx_start_arg;
   
   /* Step 10: */ 
   sigprocmask(SIG_SETMASK, &mctx_creat_sigs, NULL);

   /* Step 11: */ 
   mctx_start_func = mctx_creat_func;
   mctx_start_arg = mctx_creat_arg;

   /* Step 12 & Step 13: */ 
   mctx_switch(mctx_creat, &mctx_caller);

   /* The thread ¿¿magically¿¿ starts... */ 
   mctx_start_func(mctx_start_arg);

   /* NOTREACHED */ 
   abort();
}

