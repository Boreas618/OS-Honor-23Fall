#pragma once
#include <common/list.h>
#include <common/sem.h>
#include <fs/block_device.h>
#include <fs/defines.h>

/* Maximum number of distinct blocks that one atomic operation can hold. */
#define OP_MAX_NUM_BLOCKS 10

/*
 * The threshold of block cache to start eviction.
 * 
 * If the number of cached blocks is no less than this threshold, we can evict some blocks in `acquire` to keep block cache small.
 */
#define EVICTION_THRESHOLD 20

/*
 * A block in block cache.
 * 
 * You can add any member to this struct as you want.
 */
typedef struct {
    /*
     * The corresponding block number on disk.
     *
     * It should be protected by the global lock of the block cache.
     * It is required by our test. DO NOT remove it.
     */
    usize block_no;

    /* 
     * List this block into a linked list.
     * 
     * It should be protected by the global lock of the block cache.
     */
    ListNode node;

    /* 
     * Is the block already acquired by some thread or process?
     * 
     * It should be protected by the global lock of the block cache.
     */
    bool acquired;

    /* 
     * Is the block pinned?
     * A pinned block should not be evicted from the cache. e.g. it is dirty.
     * 
     * It should be protected by the global lock of the block cache.
     */
    bool pinned;

    /* The sleep lock protecting `valid` and `data`. */
    SleepLock lock;

    /* 
     * Is the content of block loaded from disk?
     * 
     * You may find it useless and it *is*. It is just a test flag read by our test. In your code, you should:
     * - set `valid` to `false` when you allocate a new `Block` struct.
     * - set `valid` to `true` only after you load the content of block from disk.
     * 
     * It is required by our test. DO NOT remove it.
     */
    bool valid;

    /* The real in-memory content of the block on disk. */
    u8 data[BLOCK_SIZE];
} Block;

/* An atomic operation context. */
typedef struct {
    /*
     * The number of operation remaining in this atomic operation.
     *
     * If `rm` is 0, any **new** `sync` will panic.
     */
    usize rm;

    /* 
     * A timestamp (i.e. an ID) to identify this atomic operation.

     * Your implementation does NOT have to use this field.
     * It is required by our test. DO NOT remove it.
     */
    usize ts;
} OpContext;

/* 
 * Interface for block caches.
 *
 * Here are some examples of using block caches as a normal caller:
 * 
 * ## Read a block
 * 
 * ```c
 * Block *block = bcache->acquire(block_no);
 * // ... read block->data here ...
 * 
 * bcache->release(block);
 * ```
 * 
 * ## Write a block
 * 
 * ```c
 * // Define an atomic operation context.
 * OpContext ctx;
 * 
 * // Begin an atomic operation.
 * bcache->begin_op(&ctx);
 * Block *block = bcache->acquire(block_no);
 * 
 * // ... modify block->data here ...
 * 
 * // Notify the block cache that "I have modified block->data".
 * bcache->sync(&ctx, block);
 * bcache->release(block);
 * // end the atomic operation
 * bcache->end_op(&ctx);
 * ```
 */
typedef struct {
    /*
     * Return the number of cached blocks at this moment.
     * 
     * It is only required by our test to print statistics.
     */
    usize (*get_num_cached_blocks)();

    /*
     * Declare a block as acquired by the caller.
     * 
     * It reads the content of block at `block_no` from disk, and locks the block so that the caller can exclusively modify it.
     * It returns the pointer to the locked block.
     */
    Block *(*acquire)(usize block_no);

    /*
     * Declare an acquired block as released by the caller.
     *
     * It unlocks the block so that other threads can acquire it again.
     * It does not need to write the block content back to disk.
     */
    void (*release)(Block *block);

    /*
     * # Notes for Atomic Operations
     *
     * Atomic operation has three states:
     * 
     * - Running: this atomic operation may have more modifications.
     * - Committed: this atomic operation is ended. No more modifications.
     * - Checkpointed: all modifications have been already persisted to disk.
     * 
     * `begin_op` creates a new running atomic operation.
     * `end_op` commits an atomic operation, and waits for it to be checkpointed.
     */

    /*
     * Begin a new atomic operation and initialize `ctx`.
     *
     * If there are too many running operations (i.e. our logging is too small to hold all of them), `begin_op` 
     * should sleep until we can start a new operation.
     * 
     * `ctx` is the context to be initialized.
     * 
     * It will throw panic if `ctx` is NULL.
     */
    void (*begin_op)(OpContext *ctx);

    /*
     * Synchronize the content of `block` to disk.
     * 
     * If `ctx` is NULL, it immediately writes the content of `block` to disk.
     * 
     * However this is very dangerous, since it may break atomicity of
     * concurrent atomic operations. YOU SHOULD USE THIS MODE WITH CARE.
     * 
     * `ctx` is the atomic operation context to which this block belongs.
     * 
     * The caller must hold the lock of `block`.
     * 
     * It will throw panic if the number of blocks associated with `ctx` is larger
     * than `OP_MAX_NUM_BLOCKS` after `sync`
     */
    void (*sync)(OpContext *ctx, Block *block);

    /*
     * End the atomic operation managed by `ctx`.
     * 
     * It sleeps until all associated blocks are written to disk.
     * 
     * `ctx` the atomic operation context to be ended.
     * 
     * It will throw panic if `ctx` is NULL.
     */
    void (*end_op)(OpContext *ctx);

    /*
     * Notes For Bitmap
     * 
     * Every block on disk has a bit in bitmap, including blocks inside bitmap.
     * Usually, MBR block, super block, inode blocks, log blocks and bitmap
     * blocks are preallocated on disk, i.e. those bits for them are already set
     * in bitmap. When we allocate a new block, it usually returns a
     * data block. However, nobody can prevent you freeing a non-data block.
     */

    /*
     * Allocate a new zero-initialized block.
     * 
     * It searches bitmap for a free block, mark it allocated and
     * returns the block number.
     * 
     * `ctx`: since this function may write on-disk bitmap, it must be
     * associated with an atomic operation. The caller must ensure that
     *  `ctx` is **running**.
     * 
     * It returns the block number of the allocated block.
     * 
     * You should use `acquire`, `sync` and `release` to do disk I/O here.
     * 
     * It will throw panic if there is no free block on disk.
     */
    usize (*alloc)(OpContext *ctx);

    /*
     * Free the block at `block_no` in bitmap.
     * 
     * It will NOT panic if `block_no` is already free or invalid.
     * 
     * `ctx` since this function may write on-disk bitmap, it must be
     * associated with an atomic operation. The caller must ensure that 
     * `ctx` is **running**.
     * 
     * `block_no` the block number to be freed.
     * 
     * You should use `acquire`, `sync` and `release` to do disk I/O here.
     */
    void (*free)(OpContext *ctx, usize block_no);
} BlockCache;

/* The global block cache instance. */
extern BlockCache bcache;

/* Initialize the block cache.
 * 
 * This method is also responsible for restoring logs after system crash,
 * 
 * i.e. it should read the uncommitted blocks from log section and
 * write them back to their original positions.
 * 
 * `sblock` the loaded super block.
 * `device` the initialized block device.
 * 
 * You may want to put it into `*_init` method groups.
 */
void init_bcache(const SuperBlock *sblock, const BlockDevice *device);