#include <setjmp.h>
#include <signal.h>

#define TRUE 1
#define FALSE 0

typedef struct mctx_st {
   jmp_buf jb;
} mctx_t;

#define mctx_save(mctx) setjmp((mctx)->jb)
#define mctx_restore(mctx) longjmp((mctx)->jb, 1)
#define mctx_switch(mctx_old, mctx_new) \
   if (setjmp((mctx_old)->jb) == 0) \
      longjmp ((mctx_new)->jb, 1)

void mctx_create( 
   mctx_t *mctx, 
   void (*sf_addr)( void *), void *sf_arg, 
   void *sk_addr, size_t sk_size);


void mctx_create_boot(void);
void mctx_create_trampoline(int sig);
