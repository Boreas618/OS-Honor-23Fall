#include "file.h"
#include <fs/cache.h>
#include <fs/inode.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <lib/spinlock.h>
#include <lib/string.h>

/* The global file table. */
static struct ftable ftable;

void init_ftable() { init_spinlock(&ftable.lock); }

void init_oftable(struct oftable *oftable) {
    memset(oftable, 0, sizeof(struct oftable));
}

/* Allocate a file structure. */
struct file *file_alloc() {
    acquire_spinlock(&ftable.lock);
    for (struct file *f = ftable.file; f < ftable.file + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            release_spinlock(&ftable.lock);
            return f;
        }
    }
    release_spinlock(&ftable.lock);
    return 0;
}

/* Increment ref count for file f. */
struct file *file_dup(struct file *f) {
    acquire_spinlock(&ftable.lock);
    ASSERT(f->ref >= 1);
    f->ref++;
    release_spinlock(&ftable.lock);
    return f;
}

/**
 * Decrease the reference count of a file object.
 *
 * If f->ref == 0, really close the file and put the inode (or close the pipe).
 *
 * Since `cache.end_op` may sleep, you should not hold any lock (I mean,
 * the lock for `ftable`) when calling `end_op`! Before you put the inode,
 * release the lock first.
 */
void file_close(struct file *f) {
    acquire_spinlock(&ftable.lock);

    ASSERT(f->ref >= 1);

    // Decrement the reference count and check if it's still positive.
    if (--f->ref > 0) {
        release_spinlock(&ftable.lock);
        return;
    }

    // Copy file descriptor and reset the original.
    struct file ff = *f;
    f->ref = 0;
    f->type = FD_NONE;
    release_spinlock(&ftable.lock);

    // Handle the closure of pipe and inode accordingly.
    if (ff.type == FD_PIPE) {
        pipe_close(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE) {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, ff.ip);
        bcache.end_op(&ctx);
    }
}

/* Get metadata about file f. */
int file_stat(struct file *f, struct stat *st) {
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize file_read(struct file *f, char *addr, isize n) {
    isize r = 0;

    // Check if the file is readable.
    if (f->readable == 0)
        return -1;

    // Handle the read of a pipe.
    if (f->type == FD_PIPE) {
        r = pipe_read(f->pipe, (u64)addr, n);
    }
    // Handle the read of an inode.
    else if (f->type == FD_INODE) {
        inodes.lock(f->ip);

        // Read the inode. On a successful read, update the file offset.
        r = inodes.read(f->ip, (u8 *)addr, f->off, n);
        if (r > 0)
            f->off += r;

        inodes.unlock(f->ip);
    }
    // It is illegal to use file_read to read a directory.
    // Use opendir, readdir, and closedir to perform directory operations.
    else {
        PANIC();
    }

    return r;
}

/* Write to file f. */
isize file_write(struct file *f, char *addr, isize n) {
    isize r = 0;

    // Check if the file is writable.
    if (!f->writable)
        return -1;

    // Handle the write to a pipe.
    if (f->type == FD_PIPE) {
        r = (isize)pipe_read(f->pipe, (u64)addr, n);
    }
    // Handle the write to an inode.
    else if (f->type == FD_INODE) {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.lock(f->ip);
        // Write the inode. On a successful write, update the file offset.
        r = inodes.write(&ctx, f->ip, (u8 *)addr, f->off, n);
        if (r > 0)
            f->off += r;
        inodes.unlock(f->ip);
        bcache.end_op(&ctx);
    }
    // It is illegal to use file_read to read a directory.
    // Use opendir, readdir, and closedir to perform directory operations.
    else {
        PANIC();
    }

    return r;
}