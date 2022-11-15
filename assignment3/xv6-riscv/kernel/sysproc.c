#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "condvar.h"
#include "semaphore.h"

static int barri[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static struct cond_t cv_br;
static struct sleeplock lk_br;

#define SIZE 20

typedef struct {
   int x;
   int full;
   struct sleeplock lock;
   struct cond_t inserted;
   struct cond_t deleted;
} buffer_elem;

static buffer_elem buffer[SIZE];
static int tail = 0, head = 0;
static struct sleeplock lock_delete;
static struct sleeplock lock_insert;
static struct sleeplock lock_print;

static int buffer_sem[SIZE],nextp,nextc;
static struct sem_t pro,con,empty,full;


uint64
sys_buffer_sem_init(void){
  nextp=0;
  nextc=0;

  for(int i=0;i<SIZE;i++){
    buffer_sem[i]=-1;
  }

  sem_init(&pro,1);
  sem_init(&con,1);
  sem_init(&empty,SIZE);
  sem_init(&full,0);
  return 0;
}

uint64
sys_sem_produce(void){
  int v;
  if(argint(0, &v) < 0)
    return -1;
  sem_wait(&empty);
  sem_wait (&pro);
  buffer_sem[nextp] = v;
  nextp = (nextp+1)%SIZE;
  sem_post (&pro);
  sem_post (&full);
  return 0;
}

uint64
sys_sem_consume(void){
  int v;
  sem_wait (&full);
  sem_wait (&con);
  v = buffer_sem[nextc];
  nextc = (nextc+1)%SIZE;
  sem_post (&con);
  sem_post (&empty);
  acquiresleep(&lock_print);
  printf("%d ",v);
  releasesleep(&lock_print);
  return 0;
}

uint64
sys_buffer_cond_init(void){
  initsleeplock(&lock_delete,"delete");
  initsleeplock(&lock_insert,"insert");
  initsleeplock(&lock_print,"print");
  tail=0;
  head=0;
  for(int i=0;i<SIZE;i++){
    initsleeplock(&buffer[i].lock,"buffer");
    buffer[i].x = -1;
		buffer[i].full = 0;
  }
  return 0;
}

uint64
sys_cond_produce(void){
  int v;
  if(argint(0, &v) < 0)
    return -1;
  acquiresleep(&lock_insert);
  int index = tail;
  tail = (tail + 1) % SIZE;
  releasesleep(&lock_insert);
  acquiresleep(&buffer[index].lock);
  while (buffer[index].full) cond_wait(&buffer[index].deleted, &buffer[index].lock);
  buffer[index].x = v;
  buffer[index].full = 1;
  cond_signal(&buffer[index].inserted);
  releasesleep(&buffer[index].lock);
  return 0;
}

uint64
sys_cond_consume(void){
  int v;
  acquiresleep(&lock_delete);
  int index = head;
  head = (head + 1) % SIZE;
  releasesleep(&lock_delete);
  acquiresleep(&buffer[index].lock);
  while (!buffer[index].full) cond_wait(&buffer[index].inserted, &buffer[index].lock);
  v = buffer[index].x;
  buffer[index].full = 0;
  cond_signal(&buffer[index].deleted);
  releasesleep(&buffer[index].lock);
  acquiresleep(&lock_print);
  printf("%d ", v);
  releasesleep(&lock_print);
  return v;
}

uint64
sys_barrier_alloc(void){
  initsleeplock(&lock_print,"print");
  for(int i=0;i<10;i++){
    if(barri[i]==-1){
      barri[i]=0;
      return i;
    }
  }
  return -1;
}

uint64
sys_barrier(void){
  int pid,k,n,i;
  pid=myproc()->pid;
  if(argint(0, &k) < 0)
    return -1;
  if(argint(1, &i) < 0)
    return -1;
  if(argint(2, &n) < 0)
    return -1;
  if(barri[i]==-1)
    return -1;


  acquiresleep(&lk_br);
  acquiresleep(&lock_print);
  printf("%d: Entered barrier#%d for barrier array id %d\n",pid,k,i);
  releasesleep(&lock_print);
  barri[i]++;
  if(barri[i]==n){
    cond_broadcast(&cv_br);
    barri[i]=0;
  }else{
    cond_wait(&cv_br,&lk_br);
  }
  acquiresleep(&lock_print);
  printf("%d: Finished barrier#%d for barrier array id %d\n",pid,k,i);
  releasesleep(&lock_print);
  releasesleep(&lk_br);

  return 0;
}

uint64
sys_barrier_free(void){
  int n;
  if(argint(0, &n) < 0)
    return -1;
  barri[n]=-1;
  return 0;
}



uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_getppid(void)
{
  if (myproc()->parent) return myproc()->parent->pid;
  else {
     printf("No parent found.\n");
     return 0;
  }
}

uint64
sys_yield(void)
{
  yield();
  return 0;
}

uint64
sys_getpa(void)
{
  uint64 x;
  if (argaddr(0, &x) < 0) return -1;
  return walkaddr(myproc()->pagetable, x) + (x & (PGSIZE - 1));
}

uint64
sys_forkf(void)
{
  uint64 x;
  if (argaddr(0, &x) < 0) return -1;
  return forkf(x);
}

uint64
sys_waitpid(void)
{
  uint64 p;
  int x;

  if(argint(0, &x) < 0)
    return -1;
  if(argaddr(1, &p) < 0)
    return -1;

  if (x == -1) return wait(p);
  if ((x == 0) || (x < -1)) return -1;
  return waitpid(x, p);
}

uint64
sys_ps(void)
{
   return ps();
}

uint64
sys_pinfo(void)
{
  uint64 p;
  int x;

  if(argint(0, &x) < 0)
    return -1;
  if(argaddr(1, &p) < 0)
    return -1;

  if ((x == 0) || (x < -1) || (p == 0)) return -1;
  return pinfo(x, p);
}

uint64
sys_forkp(void)
{
  int x;
  if(argint(0, &x) < 0) return -1;
  return forkp(x);
}

uint64
sys_schedpolicy(void)
{
  int x;
  if(argint(0, &x) < 0) return -1;
  return schedpolicy(x);
}
