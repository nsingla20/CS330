#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[]){
    if(argc!=3){
        printf("Invalid arguments!\nFormat:\nforksleep [int] [int]\n");
        exit(0);
    }
    int m=atoi(argv[1]),n=atoi(argv[2]);
    int x=fork();
    if(n==0){
        if(x==0){
            sleep(m);
            printf("%d: Child\n",getpid());
        }else if(x>0){
            printf("%d: Parent\n",getpid());
        }else{
            printf("fork error\n");
        }
    }else if(n==1){
        if(x==0){
            printf("%d: Child\n",getpid());
        }else if(x>0){
            sleep(m);
            printf("%d: Parent\n",getpid());
        }else{
            printf("fork error\n");
        }
    }else{
        printf("ERROR: n can take {0,1} only\n");
    }
    exit(0);
}