#pragma once

#include <fs/inode.h>
#include <lib/defines.h>

#define BACKSPACE 0x100
#define C(x) ((x) - '@')
#define INPUT_BUF_SIZE 128

extern InodeTree inodes;

/* The console descriptor structure */
struct console {
    struct spinlock lock;
    struct semaphore sem;
    char buf[INPUT_BUF_SIZE];
    usize read_idx;
    usize write_idx;
    usize edit_idx;
};

void console_intr();
isize console_write(Inode *ip, char *buf, isize n);
isize console_read(Inode *ip, char *dst, isize n);