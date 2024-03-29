#pragma once

#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <lib/string.h>

#define BSIZE 512

#define B_VALID 0x2 /* Buffer has been read from disk. */
#define B_DIRTY 0x4 /* Buffer needs to be written to disk. */

struct buf {
	int flags;
	u32 blockno;
	u8 data[BSIZE]; // 1B*512
	struct list_node bq_node;
	struct semaphore sem;
};