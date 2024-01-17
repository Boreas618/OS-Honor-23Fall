#pragma once

#include <fs/defines.h>
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
    int ref;
    bool readable, writable;
    union {
        struct pipe *pipe;
        struct inode *ip;
    };
    usize off;
};

typedef struct file File;

struct ftable {
    struct spinlock lock;
    struct file file[NFILE];
};

struct oftable {
    struct file* ofiles[NOFILE];
};

void init_ftable();
void init_oftable(struct oftable *);
struct file *file_alloc();
struct file *file_dup(struct file *f);
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