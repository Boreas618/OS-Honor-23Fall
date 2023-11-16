#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/*
 * The private reference to the super block.
 * 
 * We need these two variables because we allow the caller to specify the block device 
 * and super block to use.
 * Correspondingly, you should NEVER use global instance of them, e.g. `get_super_block`, 
 * `block_device`.
 */
static const SuperBlock *sblock;

/* The reference to the underlying block device. */
static const BlockDevice *device; 

/* 
 * Global lock for block cache. Use it to protect anything you need. e.g. the list of
 * allocated blocks, etc. 
 */
static SpinLock lock;

/* 
 * The list of all allocated in-memory block.
 * We use a linked list to manage all allocated cached blocks.
 * You can implement your own data structure if you like better performance.
 */
static List blocks;

/* In-memory copy of log header block. */
static LogHeader header;

/* 
 * Maintain other logging states.
 *
 * You may wonder where we store some states, e.g.
 * - How many atomic operations are running?
 * - Are we checkpointing?
 * - How to notify `end_op` that a checkpoint is done?
 * 
 * See cache_begin_op, cache_end_op, cache_sync.
 */
struct {
    /* your fields here */
} log;

define_early_init(cache) {
    init_spinlock(&lock);
    list_init(&blocks);
}

Block* _fetch_cached(usize block_no);

bool _evict();

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
    return (usize) blocks.size;
}

static Block *cache_acquire(usize block_no) {
    Block *b = NULL;
    _acquire_spinlock(&lock);

    if (b = _fetch_cached(block_no)) {
        if (!b->acquired) {
            _get_sem(&b->lock);
            b->acquired = true;
            _release_spinlock(&lock);
            return b;
        }
        while (b->acquired) {
            _release_spinlock(&lock);
            _wait_sem(&b->lock, false);
            _acquire_spinlock(&lock);
            if (_get_sem(&b->lock)) {
                b->acquired = true;
                _release_spinlock(&lock);
                return b;
            }
        }
    }

    ASSERT(b == NULL);

    // Best-effort eviction
    (blocks.size >= EVICTION_THRESHOLD) && (_evict());
    
    b = (Block*) kalloc(sizeof(Block));
    init_block(b);
    b->block_no = block_no;
    b->acquired = true;
    _get_sem(&b->lock);
    list_push_back(&blocks, &b->node);
    device_read(b);
    _release_spinlock(&lock);
    return b;
}

static void cache_release(Block *block) {
    ASSERT(block->acquired);
    _acquire_spinlock(&lock);
    block->acquired = false;
    _post_sem(&block->lock);
    _release_spinlock(&lock);
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

Block* _fetch_cached(usize block_no) {
    for (ListNode* p = list_head(blocks);;p = p->next) {
        Block *b = container_of(p, Block, node);
        if (b->block_no == block_no)
            return b;
        else if (p->next == list_head(blocks))
            return NULL;
    }
}

bool _evict() {
    for (ListNode* p = list_head(blocks);;p = p->next) {
        Block *b = container_of(p ,Block, node);
        if (!b->pinned) {
            list_remove(&blocks, p);
            return true;
        }
        if (p->next == list_head(blocks))
            return false;
    }
}