#include <fs/file.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <lib/cond.h>
#include <lib/string.h>
#include <proc/sched.h>
#include <lib/printk.h>

int pipe_alloc(struct file **f0, struct file **f1) {
    struct pipe *pi;

    if (!(*f0 = file_alloc()) || !(*f1 = file_alloc()))
        PANIC();
    if (!(pi = (struct pipe *)kalloc(sizeof(struct pipe))))
        PANIC();
    // Initialize the pipe.
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;

    init_spinlock(&pi->lock);
    init_sem(&(pi->wlock), 0);
    init_sem(&(pi->rlock), 0);
    pi->nread = 0;
    pi->nwrite = 0;
    pi->readopen = 1;
    pi->writeopen = 1;

    // Initialize the file descriptors of the pipe.
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
    return 0;
}

void pipe_close(struct pipe *pi, int writable) {
    acquire_spinlock(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        cond_broadcast(&pi->rlock);
    } else {
        pi->readopen = 0;
        cond_broadcast(&pi->wlock);
    }

    if (pi->readopen == 0 && pi->writeopen == 0) {
        release_spinlock(&pi->lock);
        kfree((void *)pi);
    } else {
        release_spinlock(&pi->lock);
    }
}

int pipe_write(struct pipe *pi, u64 addr, int n) {
    int i = 0;
    struct proc *pr = thisproc();

    acquire_spinlock(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || pr->killed) {
            release_spinlock(&pi->lock);
            return -1;
        }

        if (pi->nwrite == pi->nread + PIPE_SIZE) {
            cond_broadcast(&pi->rlock);
            cond_wait(&pi->wlock, &pi->lock);
        } else {
            pi->data[pi->nwrite++ % PIPE_SIZE] = *(char *)(addr + i);
            i++;
        }
    }
    cond_broadcast(&pi->rlock);
    release_spinlock(&pi->lock);
    return i;
}

int pipe_read(struct pipe *pi, u64 addr, int n) {
    int i;
    struct proc *pr = thisproc();

    acquire_spinlock(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen) {
        if (pr->killed) {
            release_spinlock(&pi->lock);
            return -1;
        }
        cond_wait(&pi->rlock, &pi->lock);
    }

    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite)
            break;
        *(char *)(addr + i) = pi->data[pi->nread++ % PIPE_SIZE];
    }
    cond_broadcast(&pi->wlock);
    release_spinlock(&pi->lock);
    return i;
}