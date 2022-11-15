#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "condvar.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"


void cond_wait (struct cond_t *cv, struct sleeplock *lock){
    while(!cv->signal){
        condsleep(cv,lock);
    }
}
void cond_signal (struct cond_t *cv){
    cv->signal = 1;
    wakeupone(cv);
}
void cond_broadcast (struct cond_t *cv){
    cv->signal = 1;
    wakeup(cv);
}
