#pragma once

#include <fs/inode.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <sys/stat.h>

#define FILE_NAME_MAX_LENGTH 14
#define FSSIZE 1000 // Size of file system in blocks
#define NFILE 65536 // Maximum number of open files in the whole system.
#define NOFILE 128  // Maximum number of open files of a process.

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
int file_stat(struct file *f, struct stat *st);
isize file_read(struct file *f, char *addr, isize n);
isize file_write(struct file *f, char *addr, isize n);