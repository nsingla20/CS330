#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    if(argc!=3){
        printf("Usage: primefactors <n[+int]> <x[+int]>\n");
        exit(0);
    }
    int n=atoi(argv[1]),x=atoi(argv[2]);
    if(n<1){
        printf("Usage: primefactors <n[+int]> <x[+int]>\n");
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