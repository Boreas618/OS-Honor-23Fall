#pragma once
#include <fs/block_device.h>
#include <lib/list.h>
#include <lib/sem.h>

/* Maximum number of distinct blocks that one atomic operation can hold. */
#define OP_MAX_NUM_BLOCKS 10

/*
 * The threshold of block cache to start eviction.
 *
 * If the number of cached blocks is no less than this threshold, we can evict
 * some blocks in `acquire` to keep block cache small.
 */
#define EVICTION_THRESHOLD 20

/**
 * block - a block in block cache.
 *
 * @block_no: The corresponding block number on disk.
 * @node: List this block into a linked list.
 * @acquired: Is the block already acquired by some thread or process?
 * @pinned: Is the block pinned?
 * @lock:  The sleep lock protecting `valid` and `data`.
 * @valid: Is the content of block loaded from disk?
 * @data: The real in-memory content of the block on disk.
 */
typedef struct block {
	usize block_no;
	ListNode node;
	bool acquired;
	bool pinned;
	struct semaphore lock;
	bool valid;
	u8 data[BLOCK_SIZE];
} Block;

/**
 * op_ctx - an atomic operation context. 
 * 
 * @rm: The number of operation remaining in this atomic operation.
 * @ts: A timestamp (i.e. an ID) to identify this atomic operation.
 */
typedef struct op_ctx {
	usize rm;
	usize ts;
} OpContext;

typedef struct block_cache {
	usize (*get_num_cached_blocks)();
	Block *(*acquire)(usize block_no);
	void (*release)(Block *block);
	void (*begin_op)(OpContext *ctx);
	void (*sync)(OpContext *ctx, Block *block);
	void (*end_op)(OpContext *ctx);
	usize (*alloc)(OpContext *ctx);
	void (*free)(OpContext *ctx, usize block_no);
} BlockCache;

/* The global block cache instance. */
extern BlockCache bcache;

void init_bcache(const struct super_block *sblock,
		 const struct block_device *device);