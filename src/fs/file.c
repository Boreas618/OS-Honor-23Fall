#include "file.h"
#include <lib/defines.h>
#include <lib/spinlock.h>
#include <lib/sem.h>
#include <fs/inode.h>
#include <lib/list.h>
#include <kernel/mem.h>

// The global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* TODO: LabFinal */
    return 0;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* TODO: LabFinal */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* TODO: LabFinal */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* TODO: LabFinal */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* TODO: LabFinal */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* TODO: LabFinal */
    return 0;
}