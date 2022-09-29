#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
toint(const char *s)
{
  int n;
  int neg=(*s=='-');
  if(neg)s++;
  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  if(neg==1){
    n=-n;
  }
  return n;
}
int main(int argc, char *argv[]){
    if(argc!=3){
        printf("Usage: primefactors <n[+int]> <x[int]>\n");
        exit(0);
    }
    int n=toint(argv[1]),x=toint(argv[2]);
    if(n<1){
        printf("Usage: primefactors <n[+int]> <x[int]>\n");
        exit(0);
    }
    while(n){
        int pip[2];
        pipe(pip);
        int f=fork();
        if(f>0){
            close(pip[0]);
            x+=getpid();
            printf("%d: %d\n",getpid(),x);
            write(pip[1],&x,sizeof(x));
            close(pip[1]);
            wait(0);
            exit(0);
        }else if(f==0){
            n--;
            close(pip[1]);
            read(pip[0],&x,sizeof(x));
            close(pip[0]);
        }else{
            printf("fork error\n");
            exit(0);
        }
    }
    exit(0);
}