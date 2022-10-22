#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "procstat.h"
#include "batchstat.h"

int cur_sched_policy;

struct batchstat batchst;

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  cur_sched_policy=SCHED_PREEMPT_RR;
  batchst.st_time=__INT32_MAX__;
  batchst.end_time=0;
  batchst.avg_wt=0;
  batchst.avg_tr=0;
  batchst.avg_comp=0;
  batchst.min_comp=__INT32_MAX__;
  batchst.max_comp=0;
  batchst.n_CPU_brst=0;
  batchst.avg_CPU_brst=0;
  batchst.min_CPU_brst=__INT32_MAX__;
  batchst.max_CPU_brst=0;
  batchst.n_CPU_brst_est=0;
  batchst.avg_CPU_brst_est=0;
  batchst.min_CPU_brst_est=__INT32_MAX__;
  batchst.max_CPU_brst_est=0;
  batchst.n_CPU_brst_err=0;
  batchst.avg_CPU_brst_err=0;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  uint xticks;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);

  p->ctime = xticks;
  p->stime = -1;
  p->endtime = -1;
  p->is_forkp=0;
  p->s=0;
  p->t=0;
  p->bp = 0;
  p->cpu_us=0;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  uint xticks;
  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;
  p->en_runab=xticks;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

int
forkf(uint64 faddr)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;
  // Make child to jump to function
  np->trapframe->epc = faddr;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
forkp(int bp)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  np->is_forkp=1;
  batchst.n++;
  np->bp=bp;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  uint xticks;
  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;
  p->en_runab=xticks;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();
  uint xticks;

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  p->t=xticks;
  p->endtime = xticks;

  if(p->is_forkp){
    batchst.end_time=max(batchst.end_time,xticks);
    batchst.avg_tr+=(p->endtime-p->ctime);
    batchst.avg_comp+=xticks;
    batchst.min_comp=min(batchst.min_comp,xticks);
    batchst.max_comp=max(batchst.max_comp,xticks);
  }

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

int
waitpid(int pid, uint64 addr)
{
  struct proc *np;
  struct proc *p = myproc();
  int found=0;

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for child with pid
    for(np = proc; np < &proc[NPROC]; np++){
      if((np->parent == p) && (np->pid == pid)){
	found = 1;
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        if(np->state == ZOMBIE){
           if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
             release(&np->lock);
             release(&wait_lock);
             return -1;
           }
           freeproc(np);
           release(&np->lock);
           release(&wait_lock);
           return pid;
	}

        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!found || p->killed){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

//run a process
void run_proc(struct proc *p,struct cpu *c){
  // printf("Running:%d\n",p->pid);
  uint xticks;
  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;

  if(p->is_forkp){
    batchst.st_time=min(batchst.st_time,xticks);
  }

  // Switch to chosen process.  It is the process's job
  // to release its lock and then reacquire it
  // before jumping back to us.
  p->state = RUNNING;
  batchst.avg_wt+=(xticks-p->en_runab);
  uint st=xticks;
  c->proc = p;
  swtch(&c->context, &p->context);

  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;

  if(!p->is_forkp){
    return;
  }

  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;

  uint end=p->t;

  uint brust=end-st;
  if(p->s>0){
    batchst.n_CPU_brst_est++;
    batchst.avg_CPU_brst_est+=p->s;
    batchst.max_CPU_brst_est=max(batchst.max_CPU_brst_est,p->s);
    batchst.min_CPU_brst_est=min(batchst.min_CPU_brst_est,p->s);
  }
  if(brust>0){
    batchst.n_CPU_brst++;
    batchst.avg_CPU_brst+=brust;
    batchst.max_CPU_brst=max(batchst.max_CPU_brst,brust);
    batchst.min_CPU_brst=min(batchst.min_CPU_brst,brust);
  }
  if(p->s>0&&brust>0){
    batchst.n_CPU_brst_err++;
    batchst.avg_CPU_brst_err+=(brust>p->s?brust-p->s:p->s-brust);
  }

  p->s=brust-(SCHED_PARAM_SJF_A_NUMER*brust)/SCHED_PARAM_SJF_A_DENOM+(SCHED_PARAM_SJF_A_NUMER*(p->s))/SCHED_PARAM_SJF_A_DENOM;
  
}

void sched_UNIX(){
  struct proc *p,*p_to_run;
  struct cpu *c = mycpu();
  c->proc=0;
  
  intr_on();
  for(p=proc,p_to_run=proc;p<&proc[NPROC];p++){
    if(p->state==RUNNABLE){
      p_to_run=p;
      break;
    }
  }
  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->state==RUNNABLE){
      p->cpu_us/=2;
    }
    release(&p->lock);
  }
  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->state==RUNNABLE){
      if(!p->is_forkp){
        run_proc(p,c);
        release(&p->lock);
        return;
      }
      int x=p->bp+p->cpu_us/2;
      int y=p_to_run->bp+p_to_run->cpu_us/2;
      if(x<y){
        p_to_run=p;
      }
    }
    release(&p->lock);
  }
  acquire(&p_to_run->lock);
  run_proc(p_to_run,c);
  release(&p_to_run->lock);
}

void sched_SJF(){
  struct proc *p,*p_to_run;
  struct cpu *c = mycpu();
  c->proc=0;
  
  intr_on();
  for(p=proc,p_to_run=proc;p<&proc[NPROC];p++){
    if(p->state==RUNNABLE){
      p_to_run=p;
      break;
    }
  }
  for(p=proc;p<&proc[NPROC];p++){
    acquire(&p->lock);
    if(p->state == RUNNABLE){
      if(!p->is_forkp){
        run_proc(p,c);
        release(&p->lock);
        return;
      }
      if(p->s<p_to_run->s){
        p_to_run=p;
      }
    }
    release(&p->lock);
  }
  acquire(&p_to_run->lock);
  run_proc(p_to_run,c);
  release(&p_to_run->lock);
}

void sched_FCFS_RR(){
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        
        run_proc(p,c);

        if(cur_sched_policy!=SCHED_PREEMPT_RR&&cur_sched_policy!=SCHED_NPREEMPT_FCFS){
          release(&p->lock);
          return;
        }
      }
      release(&p->lock);
    }
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  for(;;){
    if(cur_sched_policy==SCHED_NPREEMPT_FCFS){
      sched_FCFS_RR();
    }else if(cur_sched_policy==SCHED_NPREEMPT_SJF){
      sched_SJF();
    }else if(cur_sched_policy==SCHED_PREEMPT_RR){
      sched_FCFS_RR();
    }else if(cur_sched_policy==SCHED_PREEMPT_UNIX){
      sched_UNIX();
    }else{
      panic("No such sched policy");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  uint xticks;
  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;
  p->en_runab=xticks;
  p->cpu_us+=SCHED_PARAM_CPU_USAGE;
  p->t=xticks;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;
  uint xticks;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);

  myproc()->stime = xticks;

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->cpu_us+=SCHED_PARAM_CPU_USAGE/2;
  p->state = SLEEPING;

  uint xticks;
  if (!holding(&tickslock)) {
    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
  }
  else xticks = ticks;
  p->t=xticks;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        uint xticks;
        if (!holding(&tickslock)) {
          acquire(&tickslock);
          xticks = ticks;
          release(&tickslock);
        }
        else xticks = ticks;
        p->en_runab=xticks;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
        uint xticks;
        if (!holding(&tickslock)) {
          acquire(&tickslock);
          xticks = ticks;
          release(&tickslock);
        }
        else xticks = ticks;
        p->en_runab=xticks;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// Print a process listing to console with proper locks held.
// Caution: don't invoke too often; can slow down the machine.
int
ps(void)
{
   static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep",
  [RUNNABLE]  "runble",
  [RUNNING]   "run",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;
  int ppid, pid;
  uint xticks;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED) {
      release(&p->lock);
      continue;
    }
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    pid = p->pid;
    release(&p->lock);
    acquire(&wait_lock);
    if (p->parent) {
       acquire(&p->parent->lock);
       ppid = p->parent->pid;
       release(&p->parent->lock);
    }
    else ppid = -1;
    release(&wait_lock);

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);

    printf("pid=%d, ppid=%d, state=%s, cmd=%s, ctime=%d, stime=%d, etime=%d, size=%p", pid, ppid, state, p->name, p->ctime, p->stime, (p->endtime == -1) ? xticks-p->stime : p->endtime-p->stime, p->sz);
    printf("\n");
  }
  return 0;
}

int
pinfo(int pid, uint64 addr)
{
   struct procstat pstat;

   static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep",
  [RUNNABLE]  "runble",
  [RUNNING]   "run",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;
  uint xticks;
  int found=0;

  if (pid == -1) {
     p = myproc();
     acquire(&p->lock);
     found=1;
  }
  else {
     for(p = proc; p < &proc[NPROC]; p++){
       acquire(&p->lock);
       if((p->state == UNUSED) || (p->pid != pid)) {
         release(&p->lock);
         continue;
       }
       else {
         found=1;
         break;
       }
     }
  }
  if (found) {
     if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
         state = states[p->state];
     else
         state = "???";

     pstat.pid = p->pid;
     release(&p->lock);
     acquire(&wait_lock);
     if (p->parent) {
        acquire(&p->parent->lock);
        pstat.ppid = p->parent->pid;
        release(&p->parent->lock);
     }
     else pstat.ppid = -1;
     release(&wait_lock);

     acquire(&tickslock);
     xticks = ticks;
     release(&tickslock);

     safestrcpy(&pstat.state[0], state, strlen(state)+1);
     safestrcpy(&pstat.command[0], &p->name[0], sizeof(p->name));
     pstat.ctime = p->ctime;
     pstat.stime = p->stime;
     pstat.etime = (p->endtime == -1) ? xticks-p->stime : p->endtime-p->stime;
     pstat.size = p->sz;
     if(copyout(myproc()->pagetable, addr, (char *)&pstat, sizeof(pstat)) < 0) return -1;
     return 0;
  }
  else return -1;
}
int schedpolicy(int p){
  int x=cur_sched_policy;
  cur_sched_policy=p;
  return x;
}
int get_cur_sched_policy(){
  return cur_sched_policy;
}
void print_batch(){
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->is_forkp){
      waitpid(p->pid,0);
    }
  }

  batchst.avg_tr/=batchst.n;
  batchst.avg_wt/=batchst.n;
  batchst.avg_comp/=batchst.n;
  batchst.avg_CPU_brst/=batchst.n_CPU_brst;
  batchst.avg_CPU_brst_err/=batchst.n_CPU_brst_err;
  batchst.avg_CPU_brst_est/=batchst.n_CPU_brst_est;
  printf("\n");
  printf("Batch execution time: %d\n",batchst.end_time-batchst.st_time);
   printf("Average turn-around time: %d\n",batchst.avg_tr);
   printf("Average waiting time: %d\n",batchst.avg_wt);
   printf("Completion time: avg: %d, max: %d, min: %d\n",batchst.avg_comp,batchst.max_comp,batchst.min_comp);
   if(cur_sched_policy==SCHED_NPREEMPT_SJF){
      printf("CPU bursts: count: %d, avg: %d, max: %d, min: %d\n",batchst.n_CPU_brst,batchst.avg_CPU_brst,batchst.max_CPU_brst,batchst.min_CPU_brst);
      printf("CPU burst estimates: count: %d, avg: %d, max: %d, min: %d\n",batchst.n_CPU_brst_est,batchst.avg_CPU_brst_est,batchst.max_CPU_brst_est,batchst.min_CPU_brst_est);
      printf("CPU burst estimation error: count: %d, avg: %d\n",batchst.n_CPU_brst_err,batchst.avg_CPU_brst_err);
   }
  batchst.st_time=__INT32_MAX__;
  batchst.end_time=0;
  batchst.avg_wt=0;
  batchst.avg_tr=0;
  batchst.avg_comp=0;
  batchst.min_comp=__INT32_MAX__;
  batchst.max_comp=0;
  batchst.n_CPU_brst=0;
  batchst.avg_CPU_brst=0;
  batchst.min_CPU_brst=__INT32_MAX__;
  batchst.max_CPU_brst=0;
  batchst.n_CPU_brst_est=0;
  batchst.avg_CPU_brst_est=0;
  batchst.min_CPU_brst_est=__INT32_MAX__;
  batchst.max_CPU_brst_est=0;
  batchst.n_CPU_brst_err=0;
  batchst.avg_CPU_brst_err=0;
}