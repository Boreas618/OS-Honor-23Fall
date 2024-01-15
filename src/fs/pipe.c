#include <kernel/mem.h>
#include <proc/sched.h>
#include <fs/pipe.h>
#include <lib/string.h>

int pipe_alloc(struct file** f0, struct file** f1) {
    struct pipe *pi;

    pi = NULL;
    *f0 = *f1 = NULL;
}

void pipe_close(struct pipe* pi, int writable) {
    // TODO
}

int pipe_write(struct pipe* pi, u64 addr, int n) {
    // TODO
}

int pipe_rRead(struct pipe* pi, u64 addr, int n) {
    // TODO
}