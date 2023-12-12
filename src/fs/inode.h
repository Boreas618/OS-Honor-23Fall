#pragma once
#include <lib/list.h>
#include <lib/rc.h>
#include <lib/spinlock.h>
#include <fs/cache.h>
#include <fs/defines.h>

/* The number of the root inode (i.e. the inode_no of `/`). */
#define ROOT_INODE_NO 1

/* An inode in memory. */
typedef struct {
    /* 
     * The lock protecting the inode metadata and its content.
     * 
     * Note that It does NOT protect `rc`, `node`, `valid`, etc, because they are
     * "runtime" variables, not "filesystem" metadata or data of the inode.
     */
    SleepLock lock;

    /*
     * The reference count of this inode.
     * 
     * Different from `Block`, an inode can be shared by multiple threads or
     * processes, so we need a reference count to track the number of
     * references to this inode.
     */
    RefCount rc;

    /* Link this inode into a linked list. */
    ListNode node;

    /* The corresponding inode number on disk. */
    usize inode_no;

    /* Whether the `entry` been loaded from disk. */
    bool valid;

    /* The real in-memory copy of the inode on disk. */
    InodeEntry entry; 
} Inode;

/* Interface of inode layer. */
typedef struct {
    /* The root inode of the file system. `init_inodes` should initialize it to a valid inode. */
    Inode* root;

    /* 
     * Allocate a new zero-initialized inode on disk.
     *
     * `type` is the type of the inode to allocate.
     * Return the number of newly allocated inode.
     * Throw panic if allocation fails (e.g. no more free inode).
     */
    usize (*alloc)(OpContext* ctx, InodeType type);

    /*
     * Acquire the sleep lock of `inode`.
     * 
     * This method should be called before any write operation to `inode` and its
     * file content.
     * 
     * If the inode has not been loaded, this method should load it from disk.
     */
    void (*lock)(Inode* inode);

    /* Release the sleep lock of `inode`. */
    void (*unlock)(Inode* inode);

    /* 
     * Synchronize the content of `inode` between memory and disk.
     * 
     * Different from block cache, this method can either read or write the inode.
     * - If `do_write` is true and the inode is valid, write the content of `inode` to disk.
     * - If `do_write` is false and the inode is invalid, read the content of `inode` from disk.
     * - If `do_write` is false and the inode is valid, do nothing.
     * 
     * Note that here "write to disk" means "sync with block cache", not "directly
     * write to underneath SD card".
     * 
     * Caller must hold the lock of `inode`.
     * 
     * It throws panic if `do_write` is true and `inode` is invalid.
     */
    void (*sync)(OpContext* ctx, Inode* inode, bool do_write);

    /*
     * Get an inode by its inode number.
     * 
     * This method should increment the reference count of the inode by one.
     * Note that it does NOT have to load the inode from disk!
     * See `sync` will be responsible to load the content of inode.
     * Return the `inode` of `inode_no`. `inode->valid` can be false.
     */
    Inode* (*get)(usize inode_no);

    /*
     * Truncate all contents of `inode`.
     * 
     * This method removes (i.e. "frees") all file blocks of `inode`.
     * Do not forget to reset related metadata of `inode`, e.g. `inode->entry.num_bytes`.
     * Note that Caller must hold the lock of `inode`.
     */
    void (*clear)(OpContext* ctx, Inode* inode);

    /*
     * Duplicate an inode.
     * 
     * Call this if you want to share an inode with others.
     * It should increment the reference count of `inode` by one.
     * Return the duplicated inode (i.e. may just return `inode`).
     */
    Inode* (*share)(Inode* inode);

    /*
     * Notify that you no longer need `inode`.
     * 
     * This method is also responsible to free the inode if no one needs it:
     * - "No one needs it" means it is useless BOTH in-memory (`inode->rc == 0`) and on-disk
     * (`inode->entry.num_links == 0`).
     * - "Free the inode" means freeing all related file blocks and the inode itself.
     * 
     * Note that do not forget `kfree(inode)` after you have done them all!
     * Note that caller must NOT hold the lock of `inode`. i.e. caller should have `unlock`ed it.
     * `clear` can be used to free all file blocks of `inode`.
     */
    void (*put)(OpContext* ctx, Inode* inode);

    /*
     * Read `count` bytes from `inode`, beginning at `offset`, to `dest`.
     * 
     * Return how many bytes you actually read.
     * Note that caller must hold the lock of `inode`.
     */
    usize (*read)(Inode* inode, u8* dest, usize offset, usize count);

    /*
     * Write `count` bytes from `src` to `inode`, beginning at `offset`.
     *
     * Return how many bytes you actually write.
     * Note that caller must hold the lock of `inode`.
     */
    usize (*write)(OpContext* ctx,
                   Inode* inode,
                   u8* src,
                   usize offset,
                   usize count);

    /*
     * Look up an entry named `name` in directory `inode`.
     * 
     * `index` is the index of found entry in this directory.
     * Return the inode number of the corresponding inode, or 0 if not found.
     * Note that caller must hold the lock of `inode`.
     * It throws panic if `inode` is not a directory.
     */
    usize (*lookup)(Inode* inode, const char* name, usize* index);

    /*
     * Insert a new directory entry in directory `inode`.
     * 
     * Add a new directory entry in `inode` called `name`, which points to inode with `inode_no`.
     * Return the index of new directory entry, or -1 if `name` already exists.
     * Note that if the directory inode is full, you should grow the size of directory inode.
     * Note that you do NOT need to change `inode->entry.num_links`. 
     * Note that caller must hold the lock of `inode`.
     * It throws panic if `inode` is not a directory.
     */
    usize (*insert)(OpContext* ctx,
                    Inode* inode,
                    const char* name,
                    usize inode_no);

    /*
     * Remove the directory entry at `index`.
     * 
     * If the corresponding entry is not used before, `remove` does nothing.
     * Note that if the last entry is removed, you can shrink the size of directory inode.
     * If you like, you can also move entries to fill the hole.
     * Note that caller must hold the lock of `inode`.
     * It throws panic if `inode` is not a directory.
     */
    void (*remove)(OpContext* ctx, Inode* inode, usize index);
} InodeTree;

/* The global inode layer instance. */
extern InodeTree inodes;

/* 
 * Initialize the inode layer.
 * 
 * Note do not forget to read the root inode from disk!
 */
void init_inodes(const SuperBlock* sblock, const BlockCache* cache);