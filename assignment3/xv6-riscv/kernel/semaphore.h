struct sem_t{
    int val;
    struct cond_t cv;
    struct sleeplock lock;
};
