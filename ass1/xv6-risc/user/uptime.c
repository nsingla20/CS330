#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[]){
    int t=uptime();
    printf("%d hours %d minutes %d seconds\n",t/36000,(t/600)%60,(t/10)%60);
    exit(0);
}