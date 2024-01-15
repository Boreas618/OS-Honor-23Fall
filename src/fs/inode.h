#pragma once
#include <lib/list.h>
#include <lib/rc.h>
#include <lib/spinlock.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <sys/stat.h>

/* An inode in memory. */
struct inode {
    struct semaphore lock;
    struct ref_count rc;        // The reference count of this inode.s
    ListNode node;              // Link this inode into a linked list.
    usize inode_no;
    bool valid;                 // Whether the `entry` been loaded from disk.
    InodeEntry entry;           // The real in-memory copy of the inode on disk.
};

/* Interface of inode layer. */
struct inode_tree {
    /* The root inode of the file system. `init_inodes` should initialize it to a valid inode. */
    Inode* root;

    /* Allocate a new zero-initialized inode on disk. */
    usize (*alloc)(OpContext* ctx, InodeType type);

    /* Acquire the sleep lock of `inode`. */
    void (*lock)(Inode* inode);

    /* Release the sleep lock of `inode`. */
    void (*unlock)(Inode* inode);

    /* Synchronize the content of `inode` between memory and disk. */
    void (*sync)(OpContext* ctx, Inode* inode, bool do_write);

    /* Get an inode by its inode number. */
    Inode* (*get)(usize inode_no);

    /* Truncate all contents of `inode`. */
    void (*clear)(OpContext* ctx, Inode* inode);

    /* Duplicate an inode. */
    Inode* (*share)(Inode* inode);

    /* Notify that you no longer need `inode`. */
    void (*put)(OpContext* ctx, Inode* inode);

    /* Read `count` bytes from `inode`, beginning at `offset`, to `dest`. */
    usize (*read)(Inode* inode, u8* dest, usize offset, usize count);

    /* Write `count` bytes from `src` to `inode`, beginning at `offset`. */
    usize (*write)(OpContext* ctx,
                   Inode* inode,
                   u8* src,
                   usize offset,
                   usize count);

    /* Look up an entry named `name` in directory `inode`. */
    usize (*lookup)(Inode* inode, const char* name, usize* index);

    /* Insert a new directory entry in directory `inode`. */
    usize (*insert)(OpContext* ctx,
                    Inode* inode,
                    const char* name,
                    usize inode_no);

    /* Remove the directory entry at `index`. */
    void (*remove)(OpContext* ctx, Inode* inode, usize index);
};


typedef struct inode Inode;
typedef struct inode_tree InodeTree;

/* The global inode layer instance. */
extern InodeTree inodes;

void init_inodes(const SuperBlock* sblock, const BlockCache* cache);
Inode* namei(const char* path, OpContext* ctx);
Inode* nameiparent(const char* path, char* name, OpContext* ctx);
void stati(Inode* ip, struct stat* st);