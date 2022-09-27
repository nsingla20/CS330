#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int x=0,y=1;
uint64 z=34;
int main(int argc, char *argv[]){
    // int a=(int)(&x);
    printf("Virtual addr : Physical addr\n");
    printf("%p : %p\n",&x,getpa(&x));
    printf("%p : %p\n",&y,getpa(&y));
    printf("%p : %p\n",&z,getpa(&z));
    printf("%d %d %d\n",x,y,z);
    exit(0);
}