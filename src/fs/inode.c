#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

static const SuperBlock* sblock;

static const BlockCache* cache;

/* Global lock for inode layer. */
static SpinLock lock;

/* The list of all allocated in-memory inodes. */
static ListNode head;


/* Return which block `inode_no` lives on. */
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

/* Return the pointer to on-disk inode. */
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

/* Return address array in indirect block. */
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

/* Initialize inode tree. */
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

/* Initialize in-memory inode. */
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    return 0;
}

static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
}

static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
}

static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
}

static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO
    return NULL;
}

static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
}

static Inode* inode_share(Inode* inode) {
    // TODO
    return 0;
}

static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
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
 * HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
 * and when it finds that the block has not been allocated, it will return 0.
 * 
 * Modified true if some new block is allocated and `inode` has been changed.
 * 
 * Return usize the block number of that block, or 0 if `ctx == NULL` and the 
 * required block has not been allocated.
 * 
 * Note that the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    return 0;
}

static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    return 0;
}

static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    return 0;
}

static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    return 0;
}

static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    return 0;
}

static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
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