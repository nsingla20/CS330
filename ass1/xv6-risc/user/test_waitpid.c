#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    int pid[5],xst[5];
    for(int i=0;i<5;i++){
        pid[i]=fork();
        if(pid[i]==0){
            sleep(10*i);
            exit(i);
        }
    }
    for(int i=0;i<5;i++){
        if(waitpid(pid[i],xst+i)!=pid[i]){
            printf("error");
            exit(0);
        }
        printf("Child with pid:%d , exit:%d\n",pid[i],xst[i]);
    }
    printf("running reverse...\n");
    for(int i=0;i<5;i++){
        pid[i]=fork();
        if(pid[i]==0){
            sleep(10*i);
            exit(i);
        }
    }
    for(int i=4;i>-1;i--){
        if(waitpid(pid[i],xst+i)!=pid[i]){
            printf("error");
            exit(0);
        }
        printf("Child with pid:%d , exit:%d\n",pid[i],xst[i]);
    }
    exit(0);
}