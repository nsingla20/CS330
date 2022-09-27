#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char *argv[]){
    printf("n");
    yield();
    printf("m");
    exit(0);
}