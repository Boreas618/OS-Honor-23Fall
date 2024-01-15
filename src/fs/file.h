#pragma once

#include <fs/defines.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/rc.h>
#include <lib/sem.h>
#include <sys/stat.h>

/**
 * file - the file descriptor
 * @type: the type of the fine. Devices can be classified as FD_INODE.
 * @ref: the reference count of the file.
 * @readable: whether the file is readable.
 * @writable: whether the file is writable.
 * @off: offset of the file in bytes. For a pipe, it is the number of bytes that
 * have been written/read.
 */
struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    struct ref_count ref;
    bool readable, writable;
    union {
        struct pipe *pipe;
        Inode *ip;
    };
    usize off;
};

typedef struct file File;

struct ftable {
    struct spinlock lock;
    struct file file[NFILE];
};

struct oftable {
    struct file file[NFILE];
};

/* Initialize the global file table. */
void init_ftable();

/* Initialize the opened file table for a process. */
void init_oftable(struct oftable *);

/* Find an unused (i.e. ref == 0) file in ftable and set ref to 1. */
struct file *file_alloc();

/* Duplicate a file object by increasing its reference count. */
struct file *file_dup(struct file *f);

/**
 * Decrease the reference count of a file object.
 *
 * If f->ref == 0, really close the file and put the inode (or close the pipe).
 *
 * Since `cache.end_op` may sleep, you should not hold any lock (I mean,
 * the lock for `ftable`) when calling `end_op`! Before you put the inode,
 * release the lock first.
 */
void file_close(struct file *f);

/** 
 * Read the metadata of a file.
 * 
 * You do not need to completely implement this method by yourself. Just call
 * `stati`. `stati` will fill `st` for an inode.
 * 
 * @st: the stat struct to be filled.
 * @return int 0 on success, or -1 on error.
 */
int file_stat(struct file *f, struct stat *st);

/**
 * Read the content of `f` with range [f->off, f->off + n).
 * 
 * @addr: the buffer to be filled.
 * @n: the number of bytes to read.
 */
isize file_read(struct file *f, char *addr, isize n);

/**
 * Write the content of `f` with range [f->off, f->off + n).
 * 
 * @addr: the buffer to be written.
 * @n: the number of bytes to write.
 */
isize file_write(struct file *f, char *addr, isize n);