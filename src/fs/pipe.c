#include <fs/file.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <lib/cond.h>
#include <lib/string.h>
#include <proc/sched.h>

int pipe_alloc(struct file **f0, struct file **f1) {
    struct pipe *pi;

    if (!(*f0 = filealloc()) || !(*f1 = filealloc()))
        PANIC();
    if (!(pi = (struct pipe *)kalloc(sizeof(struct pipe))))
        PANIC();
    // Initialize the pipe.
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;

    init_spinlock(&pi->lock);

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
        kfree(sizeof(struct pipe));
    } else {
        release_spinlock(&pi->lock);
    }
}

int pipe_write(struct pipe *pi, u64 addr, int n) {
    int i = 0;
    struct proc *pr = thisproc();

    acquire_spinlock(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || is_killed(pr)) {
            release_spinlock(&pi->lock);
            return -1;
        }

        if (pi->nwrite == pi->nread + PIPE_SIZE) {
            cond_broadcast(&pi->rlock);
            cond_wait(&pi->wlock, &pi->lock);
        } else {
            char ch;
            if(copyin(&pr->vmspace, &ch, addr + i, 1) == -1)
                 break;
            pi->data[pi->nwrite++ % PIPE_SIZE] = ch;
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
    char ch;

    acquire_spinlock(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen) {
        if (is_killed(pr)) {
            release_spinlock(&pi->lock);
            return -1;
        }
        cond_wait(&pi->rlock, &pi->lock);
    }

    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite)
            break;
        ch = pi->data[pi->nread++ % PIPE_SIZE];
        if(copyout(&pr->vmspace, addr + i, &ch, 1) == -1)
            break;
    }
    cond_broadcast(&pi->wlock);
    release_spinlock(&pi->lock);
    return i;
}