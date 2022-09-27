#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]){
    printf("Parent pid : %d\n",getpid());
    sleep(10);
    if(fork()==0){
        
        printf("Child saying Parent pid : %d\n",getppid());
        exit(0);
    }
    wait(0);
    exit(0);
}