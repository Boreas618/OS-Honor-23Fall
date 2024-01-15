#ifndef _PIPE_H
#define _PIPE_H

#include <lib/spinlock.h>
#include <lib/defines.h>
#include <fs/file.h>
#include <lib/sem.h>

#define PIPE_SIZE 512

struct pipe {
    struct spinlock lock;
    struct semaphore wlock;
    struct semaphore rlock;
    char data[PIPE_SIZE];
    u32 nread;      // number of bytes read
    u32 nwrite;     // number of bytes written
    int readopen;   // read fd is still open
    int writeopen;  // write fd is still open
};

int pipe_alloc(struct file** f0, struct file** f1);
void pipe_close(struct pipe* pi, int writable);
int pipe_write(struct pipe* pi, u64 addr, int n);
int pipe_read(struct pipe* pi, u64 addr, int n);

#endif