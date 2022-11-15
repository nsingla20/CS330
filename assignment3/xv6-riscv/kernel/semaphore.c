#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "condvar.h"
#include "semaphore.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void sem_init(struct sem_t* s,int v){
    s->val = v;
    initsleeplock(&s->lock,"sema lock");
}
void sem_wait(struct sem_t* s){
    acquiresleep(&s->lock);
    s->val--;

    if(s->val<0)cond_wait(&s->cv, &s->lock);

    releasesleep(&s->lock);
}
void sem_post(struct sem_t* s){
    acquiresleep(&s->lock);
    s->val++;
    if(s->val<=0)cond_signal(&s->cv);
    releasesleep(&s->lock);
}
