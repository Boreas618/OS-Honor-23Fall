#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

static const SuperBlock *sblock;

static const BlockCache *cache;

/* Global lock for inode layer. */
static SpinLock lock;

/* The list of all allocated in-memory inodes. */
static List cached_inodes;

/* Return which block `inode_no` lives on. */
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

/* Return the pointer to on-disk inode. */
static INLINE InodeEntry *get_entry(Block *block, usize inode_no) {
    return ((InodeEntry *)block->data) + (inode_no % INODE_PER_BLOCK);
}

/* Return address array in indirect block. */
static INLINE u32 *get_addrs(Block *block) {
    return ((IndirectBlock *)block->data)->addrs;
}

/* Initialize inode tree. */
void init_inodes(const SuperBlock *_sblock, const BlockCache *_cache) {
    init_spinlock(&lock);
    list_init(&cached_inodes);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

/* Initialize in-memory inode. */
static void init_inode(Inode *inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

static usize inode_alloc(OpContext *ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);
    usize inode_no = 1;
    while (inode_no < sblock->num_inodes) {
        usize block_no = to_block_no(inode_no);
        Block *b = cache->acquire(block_no);
        InodeEntry *ie = get_entry(b, inode_no);
        if (ie->type == INODE_INVALID) {
            memset(ie, 0, sizeof(InodeEntry));
            ie->type = type;
            cache->sync(ctx, b);
            cache->release(b);
            return inode_no;
        }
        cache->release(b);
        inode_no++;
    }
    PANIC();
    return 0;
}

static void inode_lock(Inode *inode) {
    ASSERT(inode->rc.count > 0);
    unalertable_wait_sem(&inode->lock);
}

static void inode_unlock(Inode *inode) {
    ASSERT(inode->rc.count > 0);
    post_sem(&inode->lock);
}

static void inode_sync(OpContext *ctx, Inode *inode, bool do_write) {
    usize block_no = to_block_no(inode->inode_no);
    Block *b = cache->acquire(block_no);
    // Writing operations are performed directly on the in-memory inodes.
    // To ensure data consistency with the external storage device, the process
    // involves two steps: first, copying the modified inode data to the cache,
    // and then synchronizing this cache with the storage device.
    if (do_write && inode->valid) {
        InodeEntry *ie = get_entry(b, inode->inode_no);
        memcpy(ie, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, b);
    }
    if (!do_write && !inode->valid) {
        InodeEntry *ie = get_entry(b, inode->inode_no);
        // We don't need to grab the lock for inode here.
        // The lock has been grabbed by the caller of this function.
        memcpy(&(inode->entry), ie, sizeof(InodeEntry));
        inode->valid = true;
    }
    if (do_write && !inode->valid) {
        cache->release(b);
        PANIC();
    }
    cache->release(b);
}

static Inode *inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    list_lock(&cached_inodes);
    list_forall(p, cached_inodes) {
        Inode *i = container_of(p, Inode, node);
        if (i->inode_no == inode_no) {
            increment_rc(&i->rc);
            list_unlock(&cached_inodes);
            return i;
        }
    }
    // Not found the inode with the specified inode_no in the cache.
    // Given that the caller guarantees that the inode_no
    // has been allocated, we can load the inode from the disk.

    // Allocate a inode instance in the memory and initialize it.
    Inode *new_inode = (Inode *)kalloc(sizeof(Inode));
    init_inode(new_inode);
    new_inode->inode_no = inode_no;

    // Grant the ownership of this inode by incrementing rc.
    increment_rc(&new_inode->rc);

    // Copy the entry from disk to memory.
    inode_lock(new_inode);
    inode_sync(NULL, new_inode, false);
    inode_unlock(new_inode);

    // Set the inode as valid and put it on the list of cached inodes.
    new_inode->valid = true;
    list_push_head(&cached_inodes, &new_inode->node);
    list_unlock(&cached_inodes);
    return new_inode;
}

static void inode_clear(OpContext *ctx, Inode *inode) {
    InodeEntry *ie = &inode->entry;
    // Free the direct blocks.
    for (int i = 0; i < INODE_NUM_DIRECT; i++) {
        u32 block_no = ie->addrs[i];
        if (block_no)
            cache->free(ctx, block_no);
    }
    // Free the indirect blocks.
    if (ie->indirect) {
        Block *indirect = cache->acquire(ie->indirect);
        u32 *indir_addrs = get_addrs(indirect);
        for (int i = 0; i < INODE_NUM_INDIRECT; i++) {
            u32 block_no = indir_addrs[i];
            if (block_no)
                cache->free(ctx, block_no);
        }
        cache->release(indirect);
        cache->free(ctx, ie->indirect);
    }
    // Reset the metadata.
    inode->entry.indirect = NULL;
    memset((void *)inode->entry.addrs, 0, sizeof(u32) * INODE_NUM_DIRECT);
    inode->entry.num_bytes = 0;
    inode->entry.num_links = 0;
    inode_sync(ctx, inode, true);
}

static Inode *inode_share(Inode *inode) {
    increment_rc(&inode->rc);
    return inode;
}

static void inode_put(OpContext *ctx, Inode *inode) {
    list_lock(&cached_inodes);
    // Free the inode if no one needs it
    if (inode->rc.count == 1 && inode->entry.num_links == 0) {
        inode_lock(inode);
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, true);
        inode_unlock(inode);
        kfree(inode);
    } else {
        decrement_rc(&inode->rc);
    }
    list_unlock(&cached_inodes);
}

/*
 * Get which block is the offset of the inode in.
 *
 * e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
 * of the block that contains the 1234th byte of the file
 * represented by `my_inode`.
 *
 * If a block has not been allocated for that byte, `inode_map` will
 * allocate a new block and update `my_inode`, at which time, `modified`
 * will be set to true.
 *
 * HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new
 * block, and when it finds that the block has not been allocated, it will
 * return 0.
 *
 * Modified true if some new block is allocated and `inode` has been changed.
 *
 * Return the block number of that block, or 0 if `ctx == NULL` and the
 * required block has not been allocated.
 *
 * Note that the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext *ctx, Inode *inode, usize offset,
                       bool *modified) {
    InodeEntry *entry = &inode->entry;
    ASSERT(offset < INODE_MAX_BYTES);
    usize block_no = 0;
    u32* new_block_slot = NULL;

    // The block can be directly accessed.
    if (offset / BLOCK_SIZE < INODE_NUM_DIRECT) {
        block_no = entry->addrs[offset / BLOCK_SIZE];
        new_block_slot = entry->addrs + offset / BLOCK_SIZE;
    }

    // The block cannot be accessed directly.
    if (offset / BLOCK_SIZE >= INODE_NUM_DIRECT) {
        // The indirect block is absent.
        if (!entry->indirect) {
            if (!ctx) return 0;
            entry->indirect = cache->alloc(ctx);
            *modified = true;
        }

        // Get the index in the indirect blocks.
        usize idx = offset / BLOCK_SIZE - INODE_NUM_DIRECT;

        // Get the block number from the indirect block.
        Block *indirect_b = cache->acquire(entry->indirect);
        block_no = get_addrs(indirect_b)[idx];
        new_block_slot = get_addrs(indirect_b) + idx;
        cache->release(indirect_b);
    }

    // Tackle the cases where the found block has not been allocated.
    if (!block_no && ctx) {
        block_no = cache->alloc(ctx);
        *new_block_slot = block_no;
        *modified = true;
    }
    return block_no;
}

static usize inode_read(Inode *inode, u8 *dest, usize offset, usize count) {
    InodeEntry *entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    usize cnt = 0;
    while (cnt < count) {
        // Get which block is the offset of the inode in.
        usize block_no = inode_map(NULL, inode, offset + cnt, NULL);
        // Read the block.
        Block* b = cache->acquire(block_no);
        u8* data = b->data;

        // Find the start of the data.
        usize start = (cnt == 0 ? offset % BLOCK_SIZE : 0);

        // Find the length of the data to be read.
        usize length = 0;
        if (cnt == 0)
            length = MIN(BLOCK_SIZE - start, count);
        else
            length = MIN(BLOCK_SIZE, count - cnt);
        
        // Copy the bytes to the destination.
        memcpy(dest + cnt, data + start, length);
        cache->release(b);
        cnt += length;
    }
    return count;
}

static usize inode_write(OpContext *ctx, Inode *inode, u8 *src, usize offset,
                         usize count) {
    InodeEntry *entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    usize cnt = 0;
    while (cnt < count) {
        bool modified = false;
        // Get which block is the offset of the inode in.
        usize block_no = inode_map(ctx, inode, offset + cnt, &modified);

        // Get the block.
        Block* b = cache->acquire(block_no);
        u8* data = b->data;

        // Find the start of the data.
        usize start = (cnt == 0 ? offset % BLOCK_SIZE : 0);

        // Find the length of the data to be read.
        usize length = 0;
        if (cnt == 0)
            length = MIN(BLOCK_SIZE - start, count);
        else
            length = MIN(BLOCK_SIZE, count - cnt);
        
        // Copy the bytes to the destination.
        memcpy(data + start, src + cnt, length);

        // Record the change.
        inode->entry.num_bytes += length;
        inode_sync(ctx, inode, true);
        
        cache->sync(ctx, b);
        cache->release(b);
        cnt += length;
    }
    return count;
}

static usize inode_lookup(Inode *inode, const char *name, usize *index) {
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    return 0;
}

static usize inode_insert(OpContext *ctx, Inode *inode, const char *name,
                          usize inode_no) {
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    return 0;
}

static void inode_remove(OpContext *ctx, Inode *inode, usize index) {
    // TODO
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};