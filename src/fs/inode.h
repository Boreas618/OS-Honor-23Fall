#pragma once
#include <fs/cache.h>
#include <fs/defines.h>
#include <lib/list.h>
#include <lib/rc.h>
#include <lib/spinlock.h>
#include <sys/stat.h>

#define INODE_NUM_DIRECT 12
#define INODE_NUM_INDIRECT (BLOCK_SIZE / sizeof(u32))
#define INODE_PER_BLOCK (BLOCK_SIZE / sizeof(InodeEntry))
#define INODE_MAX_BLOCKS (INODE_NUM_DIRECT + INODE_NUM_INDIRECT)
#define INODE_MAX_BYTES (INODE_MAX_BLOCKS * BLOCK_SIZE)
#define INODE_INVALID 0
#define INODE_DIRECTORY 1
#define INODE_REGULAR 2 // Regular file
#define INODE_DEVICE 3
#define ROOT_INODE_NO 1
#define FILE_NAME_MAX_LENGTH 14

typedef u16 inode_type_t;

/* On-disk inode structure. */
struct dinode {
    inode_type_t type; // `type == INODE_INVALID` implies this inode is free.
    u16 major;      // major device id, for INODE_DEVICE only.
    u16 minor;      // minor device id, for INODE_DEVICE only.
    u16 num_links;  // number of hard links to this inode in the filesystem.
    u32 num_bytes;  // number of bytes in the file, i.e. the size of file.
    u32 addrs[INODE_NUM_DIRECT]; // direct addresses/block numbers.
    u32 indirect;                // the indirect address block.
};

struct indirect_block {
    u32 addrs[INODE_NUM_INDIRECT];
};

/* Directory entry. */
struct dirent {
    u16 inode_no; // `inode_no == 0` implies this entry is free. */
    char name[FILE_NAME_MAX_LENGTH];
};

/* In-mem inode structure. */
struct inode {
    struct semaphore lock;
    struct ref_count rc;   // The reference count of this inode.s
    struct list_node node; // Link this inode into a linked list.
    usize inode_no;
    bool valid;       // Whether the `entry` been loaded from disk.
    struct dinode entry; // The real in-memory copy of the inode on disk.
};

/**
 * inode_tree: interface of inode layer.
 *
 * @root: the root inode of the file system. `init_inodes` should initialize it
 * to a valid inode.
 * @alloc: allocate a new zero-initialized inode on disk.
 * @lock: acquire the sleep lock of `inode`.
 * @unlock:  release the sleep lock of `inode`.
 * @sync: synchronize the content of `inode` between memory and disk.
 * @get: get an inode by its inode number.
 * @clear: truncate all contents of `inode`.
 * @share: duplicate an inode.
 * @put: notify that you no longer need `inode`.
 * @read: read `count` bytes from `inode`, beginning at `offset`, to `dest`.
 * @write: write `count` bytes from `src` to `inode`, beginning at `offset`.
 * @lookup: look up an entry named `name` in directory `inode`.
 * @insert: insert a new directory entry in directory `inode`.
 * @remove: remove the directory entry at `index`.
 */
struct inode_tree {
    struct inode *root;

    usize (*alloc)(OpContext *ctx, inode_type_t type);
    void (*lock)(struct inode *inode);
    void (*unlock)(struct inode *inode);
    void (*sync)(OpContext *ctx, struct inode *inode, bool do_write);
    struct inode *(*get)(usize inode_no);
    void (*clear)(OpContext *ctx, struct inode *inode);
    struct inode *(*share)(struct inode *inode);
    void (*put)(OpContext *ctx, struct inode *inode);

    usize (*read)(struct inode *inode, u8 *dest, usize offset, usize count);
    usize (*write)(OpContext *ctx, struct inode *inode, u8 *src, usize offset,
                   usize count);
    usize (*lookup)(struct inode *inode, const char *name, usize *index);
    usize (*insert)(OpContext *ctx, struct inode *inode, const char *name,
                    usize inode_no);
    void (*remove)(OpContext *ctx, struct inode *inode, usize index);
};

typedef struct inode Inode;
typedef struct inode_tree InodeTree;

/* The global inode layer instance. */
extern InodeTree inodes;

void init_inodes(const struct super_block *sblock, const BlockCache *cache);
struct inode *namei(const char *path, OpContext *ctx);
struct inode *nameiparent(const char *path, char *name, OpContext *ctx);
void stati(struct inode *ip, struct stat *st);