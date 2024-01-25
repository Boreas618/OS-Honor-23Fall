#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/spinlock.h>
#include <lib/string.h>
#include <proc/sched.h>
#include <sys/stat.h>

static const struct super_block *sblock;

static const struct block_cache *cache;

/* Global lock for inode layer. */
static struct spinlock lock;

/* The list of all allocated in-memory inodes. */
static struct list cached_inodes;

/* Return which block `inode_no` lives on. */
static inline usize to_block_no(usize inode_no)
{
	return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

/* Return the pointer to on-disk inode. */
static inline struct dinode *get_entry(struct block *block, usize inode_no)
{
	return ((struct dinode *)block->data) + (inode_no % INODE_PER_BLOCK);
}

/* Return address array in indirect block. */
static inline u32 *get_addrs(struct block *block)
{
	return ((struct indirect_block *)block->data)->addrs;
}

/* Initialize inode tree. */
void init_inodes(const struct super_block *_sblock,
		 const struct block_cache *_cache)
{
	init_spinlock(&lock);
	list_init(&cached_inodes);
	sblock = _sblock;
	cache = _cache;

	if (ROOT_INODE_NO < sblock->num_inodes)
		inodes.root = inodes.get(ROOT_INODE_NO);
	else
		printk("(warn) init_inodes: no root inode.\n");
}

static void init_inode(struct inode *inode)
{
	init_sleeplock(&inode->lock);
	init_rc(&inode->rc);
	init_list_node(&inode->node);
	inode->inode_no = 0;
	inode->valid = false;
}

static usize inode_alloc(OpContext *ctx, inode_type_t type)
{
	ASSERT(type != INODE_INVALID);

	usize inode_no = 1;
	while (inode_no < sblock->num_inodes) {
		usize block_no = to_block_no(inode_no);
		struct block *b = cache->acquire(block_no);
		struct dinode *ie = get_entry(b, inode_no);
		if (ie->type == INODE_INVALID) {
			memset(ie, 0, sizeof(struct dinode));
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

static void inode_lock(struct inode *inode)
{
	ASSERT(inode->rc.count > 0);
	unalertable_wait_sem(&inode->lock);
}

static void inode_unlock(struct inode *inode)
{
	ASSERT(inode->rc.count > 0);
	post_sem(&inode->lock);
}

static void inode_sync(OpContext *ctx, struct inode *inode, bool do_write)
{
	usize block_no = to_block_no(inode->inode_no);
	struct block *b = cache->acquire(block_no);

	// Writing operations are performed directly on the in-memory inodes.
	// To ensure data consistency with the external storage device, the process
	// involves two steps: first, copying the modified inode data to the cache,
	// and then synchronizing this cache with the storage device.
	if (do_write && inode->valid) {
		struct dinode *ie = get_entry(b, inode->inode_no);
		memcpy(ie, &inode->entry, sizeof(struct dinode));
		cache->sync(ctx, b);
	} else if (inode->valid == false) {
		struct dinode *ie = get_entry(b, inode->inode_no);
		// We don't need to grab the lock for inode here.
		// The lock has been grabbed by the caller of this function.
		memcpy(&(inode->entry), ie, sizeof(struct dinode));
		inode->valid = true;
	}

	cache->release(b);
}

static struct inode *inode_get(usize inode_no)
{
	ASSERT(inode_no > 0);
	ASSERT(inode_no < sblock->num_inodes);
	list_lock(&cached_inodes);
	list_forall(p, cached_inodes)
	{
		struct inode *i = container_of(p, struct inode, node);
		if (i->inode_no == inode_no && i->rc.count > 0 && i->valid) {
			increment_rc(&i->rc);
			list_unlock(&cached_inodes);
			return i;
		}
	}
	// Not found the inode with the specified inode_no in the cache.
	// Given that the caller guarantees that the inode_no
	// has been allocated, we can load the inode from the disk.

	// Allocate a inode instance in the memory and initialize it.
	struct inode *new_inode = (struct inode *)kalloc(sizeof(struct inode));
	init_inode(new_inode);
	new_inode->inode_no = inode_no;
	increment_rc(&new_inode->rc);
	list_push_back(&cached_inodes, &new_inode->node);
	inode_lock(new_inode);
	list_unlock(&cached_inodes);
	inode_sync(NULL, new_inode, false);
	inode_unlock(new_inode);
	return new_inode;
}

static void inode_clear(OpContext *ctx, struct inode *inode)
{
	struct dinode *ie = &inode->entry;

	// Free the direct blocks.
	for (int i = 0; i < INODE_NUM_DIRECT; i++) {
		u32 block_no = ie->addrs[i];
		if (block_no) {
			cache->free(ctx, block_no);
			ie->addrs[i] = NULL;
		}
	}

	// Free the indirect blocks.
	if (ie->indirect) {
		struct block *indirect = cache->acquire(ie->indirect);
		u32 *indir_addrs = get_addrs(indirect);
		for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
			u32 block_no = indir_addrs[i];
			if (block_no) {
				cache->free(ctx, block_no);
				indir_addrs[i] = NULL;
			}
		}
		cache->release(indirect);
		cache->free(ctx, ie->indirect);
		inode->entry.indirect = NULL;
	}

	inode->entry.num_bytes = 0;
	inode_sync(ctx, inode, true);
}

static struct inode *inode_share(struct inode *inode)
{
	list_lock(&cached_inodes);
	increment_rc(&inode->rc);
	list_unlock(&cached_inodes);
	return inode;
}

static void inode_put(OpContext *ctx, struct inode *inode)
{
	list_lock(&cached_inodes);

	// Free the inode if no one needs it
	if (inode->rc.count == 1 && inode->entry.num_links == 0 && inode->valid) {
		inode_lock(inode);
		inode->valid = false;
		list_unlock(&cached_inodes);

		inode_clear(ctx, inode);
		inode->entry.type = INODE_INVALID;
		inode_sync(ctx, inode, true);

		list_lock(&cached_inodes);
		inode_unlock(inode);
		list_remove(&cached_inodes, &inode->node);
		list_unlock(&cached_inodes);
		kfree(inode);
		return;
	}
	decrement_rc(&inode->rc);
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
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    ASSERT(offset < INODE_NUM_DIRECT + INODE_NUM_INDIRECT);
    InodeEntry* entry_ptr = &(inode->entry);
    // find in 'direct'
    if (offset < INODE_NUM_DIRECT) {
        if (entry_ptr->addrs[offset] == 0) {
            entry_ptr->addrs[offset] = cache->alloc(ctx);
            *modified = TRUE;
        }
        return entry_ptr->addrs[offset];
    }

    // find in 'indirect'
    offset -= INODE_NUM_DIRECT;
    if (entry_ptr->indirect == 0) { // alloc indrect block
        entry_ptr->indirect = cache->alloc(ctx);
    }

    Block* block = cache->acquire(entry_ptr->indirect);
    u32* addrs = get_addrs(block);
    if (addrs[offset] == 0) {
        addrs[offset] = cache->alloc(ctx);
        cache->sync(ctx, block);
        *modified = TRUE;
    } 
    cache->release(block);
    return addrs[offset];
}

static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_read(inode, (char*)dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;

    if (offset > entry->num_bytes)
		return 0;
		
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);
    
    bool modified = FALSE;
    u8* src;
    for (usize i = 0, cnt = 0; i < count; i += cnt, dest += cnt, offset += cnt) {
        usize block_no = inode_map(NULL, inode, offset / BLOCK_SIZE, &modified);
        Block* block = cache->acquire(block_no);
        if (i == 0) {
            cnt = MIN(BLOCK_SIZE - offset % (usize)BLOCK_SIZE, (count));
            src = block->data + offset % (usize)BLOCK_SIZE;
        } else {
            cnt = MIN((usize)BLOCK_SIZE, count - i);
            src = block->data;
        }
        memcpy(dest, src, cnt);
        cache->release(block);
    }
    
    return count;
}

static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    if (entry->type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_write(inode, (char*)src, count);
    }
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    bool modified = FALSE;
    u8* dest;
    for (usize i = 0, cnt = 0; i < count; i += cnt, src += cnt, offset += cnt) {
        usize block_no = inode_map(ctx, inode, offset / BLOCK_SIZE, &modified);
        Block* block = cache->acquire(block_no);
        if (i == 0) {
            cnt = MIN(BLOCK_SIZE - offset % (usize)BLOCK_SIZE, (count));
            dest = block->data + offset % (usize)BLOCK_SIZE;
        } else {
            cnt = MIN((usize)BLOCK_SIZE, count - i);
            dest = block->data;
        }
        memcpy(dest, src, cnt);
        cache->sync(ctx, block);
        cache->release(block);
    }
    if (inode->entry.num_bytes < end) {
        inode->entry.num_bytes = end;
        inode_sync(ctx, inode, TRUE);
    }
    return count;
}

static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    for (usize i = 0; i < entry->num_bytes; i += sizeof(struct dirent)) {
        struct dirent dir_entry;
        inode_read(inode, (void*)&dir_entry, i, sizeof(struct dirent));
        if (dir_entry.inode_no != 0 && strncmp(dir_entry.name, name, FILE_NAME_MAX_LENGTH) == 0) {
            if (index != NULL) {
                *index = i;
            }
            return dir_entry.inode_no;
        }
    }
    return 0;
}

static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    usize index = 0;
    //if not find
    if (inode_lookup(inode, name, &index) != 0) {
        return -1;
    }

    struct dirent dir_entry;
    // find the appropriate position
    for (index = 0; index < entry->num_bytes; index += sizeof(struct dirent)) {
        inode_read(inode, (void*)&dir_entry, index, sizeof(struct dirent));
        if (dir_entry.inode_no == 0) {
            break;
        }
    }
    // wirte
    memcpy(dir_entry.name, name, FILE_NAME_MAX_LENGTH);
    dir_entry.inode_no = inode_no;
    inode_write(ctx, inode, (void*)&dir_entry, index, sizeof(struct dirent));
    return index;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    if (index < inode->entry.num_bytes) {
        char zero[sizeof(struct dirent)] = {0};
        inode_write(ctx, inode, (void*)zero, index, sizeof(struct dirent));
    }
}


struct inode_tree inodes = {
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

/**
 * Read the next path element from `path` into `name`.
 *
 * @name: next path element.
 * @return const char* a pointer offseted in `path`, without leading `/`. If no
 * name to remove, return NULL.
 *
 * @example
 * skipelem("a/bb/c", name) = "bb/c", setting name = "a",
 * skipelem("///a//bb", name) = "bb", setting name = "a",
 * skipelem("a", name) = "", setting name = "a",
 * skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char *skipelem(const char *path, char *name)
{
	const char *s;
	int len;

	while (*path == '/')
		path++;
	if (*path == 0)
		return 0;
	s = path;
	while (*path != '/' && *path != 0)
		path++;
	len = path - s;
	if (len >= FILE_NAME_MAX_LENGTH)
		memmove(name, s, FILE_NAME_MAX_LENGTH);
	else {
		memmove(name, s, len);
		name[len] = 0;
	}
	while (*path == '/')
		path++;
	return path;
}

/**
 * Look up and return the inode for `path`.
 *
 * If `nameiparent`, return the inode for the parent and copy the final
 * path element into `name`.
 *
 * @param path a relative or absolute path. If `path` is relative, it is
 * relative to the current working directory of the process.
 * @param[out] name the final path element if `nameiparent` is true.
 * @return Inode* the inode for `path` (or its parent if `nameiparent` is true),
 * or NULL if such inode does not exist.
 *
 * @example
 * namex("/a/b", false, name) = inode of b,
 * namex("/a/b", true, name) = inode of a, setting name = "b",
 * namex("/", true, name) = NULL (because "/" has no parent!)
 */
static struct inode *namex(const char *path, bool nameiparent, char *name,
			   OpContext *ctx)
{
	struct inode *ip;
	struct inode *next;

	if (*path == '/')
		ip = inodes.share(inodes.root);
	else
		ip = inodes.share(thisproc()->cwd);

	while ((path = skipelem(path, name))) {
		inodes.lock(ip);
		if (ip->entry.type != INODE_DIRECTORY) {
			inodes.unlock(ip);
			inodes.put(ctx, ip);
			return NULL;
		}
		if (nameiparent && *path == '\0') {
			inodes.unlock(ip);
			return ip;
		}
		usize inode_no = inode_lookup(ip, name, NULL);
		if (inode_no == 0) {
			inode_unlock(ip);
			inode_put(ctx, ip);
			return NULL;
		}
		next = inode_get(inode_no);
		inodes.unlock(ip);
		inodes.put(ctx, ip);
		ip = next;
	}

	if (nameiparent) {
		inodes.put(ctx, ip);
		return NULL;
	}

	return ip;
}

struct inode *namei(const char *path, OpContext *ctx)
{
	char name[FILE_NAME_MAX_LENGTH];
	return namex(path, false, name, ctx);
}

struct inode *nameiparent(const char *path, char *name, OpContext *ctx)
{
	return namex(path, true, name, ctx);
}

/**
 * Get the stat information of `ip` into `st`.
 *
 * The caller must hold the lock of `ip`.
 */
void stati(struct inode *ip, struct stat *st)
{
	st->st_dev = 1;
	st->st_ino = ip->inode_no;
	st->st_nlink = ip->entry.num_links;
	st->st_size = ip->entry.num_bytes;
	switch (ip->entry.type) {
	case INODE_REGULAR:
		st->st_mode = S_IFREG;
		break;
	case INODE_DIRECTORY:
		st->st_mode = S_IFDIR;
		break;
	case INODE_DEVICE:
		st->st_mode = 0;
		break;
	default:
		PANIC();
	}
}