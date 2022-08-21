#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[]){
    printf("%ds\n",uptime()/10);
    exit(0);
}