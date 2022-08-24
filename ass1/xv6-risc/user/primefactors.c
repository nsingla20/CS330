#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int primes[]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97};

int main(int argc, char *argv[]){
    if(argc!=2){
        printf("Usage: primefactors <int[2,100]>\n");
        exit(0);
    }
    int n=atoi(argv[1]);
    if(n<2||n>100){
        printf("Usage: primefactors <int[2,100]>\n");
        exit(0);
    }
    int i=0;
    int pip[2];
    while(n!=1){
        pipe(pip);
        int x=fork();
        if(x==0){
            i++;
            close (pip[1]);
            read(pip[0],&n,sizeof(n));
            close(pip[0]);
        }else if(x>0){
            close(pip[0]);
            int f=0;
            while(n%primes[i]==0){
                printf("%d, ",primes[i]);
                f=1;
                n/=primes[i];
            }
            if(f==1){
                printf("[%d]\n",getpid());
            }
            write(pip[1],&n,sizeof(n));
            close(pip[1]);
            wait(0);
            exit(0);
        }else{
            printf("fork error\n");
            exit(0);
        }
    }
    exit(0);
}