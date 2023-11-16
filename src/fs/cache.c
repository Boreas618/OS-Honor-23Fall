#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/*
 * The private reference to the super block.
 * 
 * We need these two variables because we allow the caller to specify the block device and super block to use.
 * Correspondingly, you should NEVER use global instance of them, e.g. `get_super_block`, `block_device`.
 * 
 * See init_bcache
 */
static const SuperBlock *sblock;

/* The reference to the underlying block device. */
static const BlockDevice *device; 

/* Global lock for block cache. Use it to protect anything you need. e.g. the list of allocated blocks, etc. */
static SpinLock lock;

/* 
 * The list of all allocated in-memory block.
 * We use a linked list to manage all allocated cached blocks.
 * You can implement your own data structure if you like better performance.
 * 
 * See Block.
 */
static ListNode head;

/* In-memory copy of log header block. */
static LogHeader header;

/* 
 * Maintain other logging states.
 *
 * You may wonder where we store some states, e.g.
 * - How many atomic operations are running?
 * - Are we checkpointing?
 * How to notify `end_op` that a checkpoint is done?
 * See cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* your fields here */
} log;

/* Read the content from disk. */
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

/* Write the content back to disk. */
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

/* Read log header from disk. */
static INLINE void read_header() {
    device->read(sblock->log_start, (u8 *)&header);
}

/* Write log header back to disk. */
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

/* Initialize a block struct. */
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

static usize get_num_cached_blocks() {
    // TODO
    return 0;
}

static Block *cache_acquire(usize block_no) {
    // TODO
    return 0;
}

static void cache_release(Block *block) {
    // TODO
}

void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    // TODO
}

static void cache_begin_op(OpContext *ctx) {
    // TODO
}

static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
}

static void cache_end_op(OpContext *ctx) {
    // TODO
}

static usize cache_alloc(OpContext *ctx) {
    // TODO
}

static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};