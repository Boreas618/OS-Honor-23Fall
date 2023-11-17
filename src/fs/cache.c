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

enum logstate {CHILLING, TRACKING, WORKING};

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
    SpinLock lock;
    u32 contributors_cnt;
    enum logstate state;
    Semaphore work_done;
} log;

define_early_init(cache) {
    init_spinlock(&lock);
    list_init(&blocks);
}

Block* _fetch_cached(usize block_no);

bool _evict();

int write_all(bool to_log);

int spawn_ckpt();

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

    // The requested block is right in the cache.
    if (b = _fetch_cached(block_no)) {
        while (b->acquired) {
            _release_spinlock(&lock);
            unalertable_wait_sem(&b->lock);
            if (get_sem(&b->lock)) {
                b->acquired = true;
                break;
            }
        }
        if (!b->acquired)
            (void)((get_sem(&b->lock)) && (b->acquired = true));
        _boost_freq(b);
        _release_spinlock(&lock);
        return b;
    }

    // Best-effort eviction.
    (void)((blocks.size >= EVICTION_THRESHOLD) && (_evict()));
    
    // Initialize a new block cache.
    b = (Block*) kalloc(sizeof(Block));
    init_block(b);
    (void)(
        _get_sem(&b->lock) &&
        (b->acquired = true) &&  
        (b->block_no = block_no));
    
    // Push to the cache list.
    list_push_back(&blocks, &b->node);

    // Load from disk.
    device_read(b);
    b->valid = true;
    _boost_freq(b);
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

    init_spinlock(&lock);
    list_init(&blocks);

    init_spinlock(&log.lock);
    log.contributors_cnt = 0;
    log.state = CHILLING;

    spawn_ckpt();
}

static void cache_begin_op(OpContext *ctx) {
    _acquire_spinlock(&log.lock);
    if (log.state == CHILLING) log.state = TRACKING;
    log.contributors_cnt++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    _release_spinlock(&log.lock);
}

static void cache_sync(OpContext *ctx, Block *block) {
    if (!ctx)
        device_write(block);
}

static void cache_end_op(OpContext *ctx) {
    _acquire_spinlock(&log.lock);
    ASSERT(log.state == TRACKING);
    //_write_all(true);
    log.contributors_cnt--;

    if (log.contributors_cnt > 0) {
        _release_spinlock(&lock);
        unalertable_wait_sem(&(log.work_done));
        return;
    }

    if (log.contributors_cnt == 0) {
        write_all(true);
        spawn_ckpt;
        post_all_sem(&log.work_done);
    }
    _release_spinlock(&log.lock);
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

INLINE Block* _fetch_cached(usize block_no) {
    if (!blocks.size)
        return NULL;
    for (ListNode* p = list_head(blocks);;p = p->next) {
        Block *b = container_of(p, Block, node);
        if (b->block_no == block_no)
            return b;
        else if (p->next == list_head(blocks))
            return NULL;
    }
}

/* Evict 2 pages that are least frequently used. */
INLINE bool _evict() {
    bool flag = false;
    for (ListNode* p = list_head(blocks);;p = p->next) {
        Block *b = container_of(p ,Block, node);
        if (!b->pinned) {
            list_remove(&blocks, p);
            if(flag) return true;
            flag = true;
        }
        if (p->next == list_head(blocks))
            return false;
    }
    return flag;
}

INLINE void _boost_freq(Block* b) {
    blocks.head = b->node.next;
}

int write_all(bool to_log) {
    ASSERT(header.num_blocks == 0);
    _acquire_spinlock(&lock);
    int i = 0;
    if (blocks.size == 0)
        return -1;
    for (ListNode* p = list_head(blocks);; p = p->next, i++) {
        if (to_log && (i == sblock->num_log_blocks)) {
            _release_spinlock(&lock);
            return -2;
        }
        Block *b = container_of(p, Block, node);
        device->write(to_log ? (sblock->log_start + 1 + i) : b->block_no, b->data);
        if (p->next == list_head(blocks)) {
            _release_spinlock(&lock);
            return 0;
        }
    }
    PANIC();
}

int spawn_ckpt() {
    // Sync the header.
    read_header();
    
    // The transfer block which holds the block read from log.
    Block transfer_b;
    init_block(&transfer_b);

    // Read the log into the transfer block.
    // 
    // Note that the block number of the block for logging and
    // the exact block number of the data are different.
    // In order to read the log, we need to figure out the block
    // number based on the number of log_start.
    // The exact block number of the logged blocks are stored
    // in the header of the log area.
    for(int i = 0; i < header.num_blocks; i++) {
        transfer_b.block_no = sblock->log_start + 1 + i;
        device_read(&transfer_b);
        transfer_b.block_no = header.block_no[i];
        device_write(&transfer_b);
    }

    // Empty the log area.
    header.num_blocks = 0;
    write_header();
}