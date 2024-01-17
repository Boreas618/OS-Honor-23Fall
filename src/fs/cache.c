#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <lib/bitmap.h>
#include <lib/cond.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <proc/proc.h>

/* The private reference to the super block. */
static const struct super_block *sblock;

/* The reference to the underlying block device. */
static const BlockDevice *device;

/* Global lock for block cache. */
static SpinLock lock;

/* The list of all allocated in-memory block. */
static List blocks;

/* In-memory copy of log header block. */
static LogHeader header;

/* Maintain other logging states. */
struct {
    SpinLock lock;
    u32 contributors_cnt;
    Semaphore work_done;
} log;

INLINE static void boost_frequency(Block *b);

Block *_fetch_cached(usize block_no);

bool _evict();

void write_log();

void spawn_ckpt();

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

static usize get_num_cached_blocks() { return (usize)blocks.size; }

static Block *cache_acquire(usize block_no) {
    Block *b = NULL;
    acquire_spinlock(&lock);

    // The requested block is right in the cache.
    if ((b = _fetch_cached(block_no))) {
        do {
            cond_wait(&b->lock, &lock);
        } while (b->acquired);
        b->acquired = true;
        boost_frequency(b);
        release_spinlock(&lock);
        return b;
    }

    // Best-effort eviction.
    if (blocks.size >= EVICTION_THRESHOLD)
        _evict();

    // Initialize a new block cache and push it to the cache list.
    b = (Block *)kalloc(sizeof(Block));
    init_block(b);
    get_sem(&b->lock);
    b->acquired = true;
    b->block_no = block_no;
    list_push_back(&blocks, &b->node);

    // Load the content of the block from disk.
    device_read(b);
    b->valid = true;
    boost_frequency(b);
    release_spinlock(&lock);
    return b;
}

static void cache_release(Block *block) {
    ASSERT(block->acquired);
    acquire_spinlock(&lock);
    block->acquired = false;
    cond_signal(&block->lock);
    release_spinlock(&lock);
}

void init_bcache(const struct super_block *_sblock,
                 const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    init_spinlock(&lock);
    list_init(&blocks);

    init_spinlock(&log.lock);
    log.contributors_cnt = 0;
    init_sem(&log.work_done, 0);

    // Restore the log.
    spawn_ckpt();
}

static void cache_begin_op(OpContext *ctx) {
    acquire_spinlock(&log.lock);
    log.contributors_cnt++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    release_spinlock(&log.lock);
}

static void cache_sync(OpContext *ctx, Block *block) {
    if (!ctx) {
        device_write(block);
        return;
    }

    // Detect if this block has a place in the log section
    // If so, we are free to go.
    acquire_spinlock(&log.lock);
    for (usize i = 0; i < header.num_blocks; i++) {
        if (header.block_no[i] == block->block_no) {
            release_spinlock(&log.lock);
            return;
        }
    }

    // Reach the quota of ctx in terms of atomic operations.
    if (ctx->rm == 0)
        PANIC();

    // If the block is not in the log, we open a new log block for this block.
    header.num_blocks++;
    header.block_no[header.num_blocks - 1] = block->block_no;
    block->pinned = true;
    ctx->rm--;
    release_spinlock(&log.lock);
}

static void cache_end_op(OpContext *ctx) {
    (void)ctx;
    acquire_spinlock(&log.lock);
    log.contributors_cnt--;
    // If there are other contributors to the log, we wait for them to complete
    if (log.contributors_cnt > 0) {
        cond_wait(&log.work_done, &log.lock);
        release_spinlock(&log.lock);
        return;
    }
    // After all the contributors call end_op, we write the cache back to the
    // log, sync the header in memory with the header in disk and finally write
    // the checkpoint to the disk.
    if (log.contributors_cnt == 0) {
        write_log();
        write_header();
        spawn_ckpt();
        cond_broadcast(&log.work_done);
    }
    release_spinlock(&log.lock);
}

static usize cache_alloc(OpContext *ctx) {
    if (ctx->rm <= 0)
        PANIC();

    usize num_bitmap_blocks =
        (sblock->num_data_blocks + BIT_PER_BLOCK - 1) / BIT_PER_BLOCK;

    for (usize i = 0; i < num_bitmap_blocks; i++) {
        // Acquire the bitmap block.
        Block *bm_block = cache_acquire(sblock->bitmap_start + i);
        for (usize j = 0; j < BLOCK_SIZE * 8; j++) {
            // The index in the bitmap is beyond the number of blocks
            if (i * BLOCK_SIZE * 8 + j >= sblock->num_blocks) {
                cache_release(bm_block);
                goto not_found;
            }
            // The block is free.
            if (!bitmap_get((BitmapCell *)bm_block->data, j)) {
                Block *b = cache_acquire(i * BLOCK_SIZE * 8 + j);
                // Zero the block and sync the change
                memset(b->data, 0, BLOCK_SIZE);
                cache_sync(ctx, b);
                // Set the bit map and sync the change
                bitmap_set((BitmapCell *)bm_block->data, j);
                cache_sync(ctx, bm_block);
                // The work is done. Release the blocks.
                cache_release(b);
                cache_release(bm_block);
                return i * BLOCK_SIZE * 8 + j;
            }
        }
        cache_release(bm_block);
    }
not_found:
    PANIC();
}

static void cache_free(OpContext *ctx, usize block_no) {
    Block *bm_block =
        cache_acquire(block_no / (BLOCK_SIZE * 8) + sblock->bitmap_start);
    bitmap_clear((BitmapCell *)bm_block->data, block_no % (BLOCK_SIZE * 8));
    cache_sync(ctx, bm_block);
    cache_release(bm_block);
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

INLINE Block *_fetch_cached(usize block_no) {
    list_forall(p, blocks) {
        Block *b = container_of(p, Block, node);
        if (b->block_no == block_no)
            return b;
    }
    return NULL;
}

/* Evict 2 pages that are least frequently used. */
INLINE bool _evict() {
    bool flag = false;
    list_forall(p, blocks) {
        Block *b = container_of(p, Block, node);
        if (!b->pinned) {
            list_remove(&blocks, p);
            if (flag)
                return true;
            flag = true;
        }
    }
    return flag;
}

/*
 * Move the block to the end of the list.
 *
 * To implement LRU replacement policy, we move the most recently used
 * block to the end of the list and evict pages from the front of the list
 * whenever we need to remove some pages.
 */
INLINE static void boost_frequency(Block *b) { blocks.head = b->node.next; }

void write_log() {
    // Walk through every block in the log and write the changes back
    // to the log. Note that we don't just sequentially write the `blocks`
    // back because the sequence of the logged blocks is specified in
    // `block_no` of the log header.
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *b = cache_acquire(header.block_no[i]);
        device->write(sblock->log_start + i + 1, b->data);
        b->pinned = false;
        cache_release(b);
    }
}

void spawn_ckpt() {
    // Sync the header.
    read_header();

    // The transfer block which holds the block read from log.
    struct block transfer_b;
    init_block(&transfer_b);

    // Read the log into the transfer block.
    //
    // Note that the block number of the block for logging and
    // the exact block number of the data are different.
    // In order to read the log, we need to figure out the block
    // number based on the number of log_start.
    // The exact block number of the logged blocks are stored
    // in the header of the log area.
    for (usize i = 0; i < header.num_blocks; i++) {
        transfer_b.block_no = sblock->log_start + 1 + i;
        device_read(&transfer_b);
        transfer_b.block_no = header.block_no[i];
        // printk("%lld %lld %lld\n", transfer_b.block_no, i,
        // header.num_blocks);
        device_write(&transfer_b);
    }

    // Empty the log section.
    header.num_blocks = 0;
    write_header();
}