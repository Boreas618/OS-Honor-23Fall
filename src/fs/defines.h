#pragma once

#include <fs/cache.h>
#include <fs/inode.h>
#include <lib/defines.h>

/**
 * Layers of File System
 *
 * +-----------------+
 * | File descriptor |
 * +-----------------+
 * |    Pathname     |
 * +-----------------+
 * |    Directory    |
 * +-----------------+
 * |      Inode      |
 * +-----------------+
 * |     Logging     |
 * +-----------------+
 * |  Buffer cache   |
 * +-----------------+
 * |      Disk       |
 * +-----------------+
 */

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
#define FSSIZE 1000 // Size of file system in blocks
#define NFILE 65536 // Maximum number of open files in the whole system.
#define NOFILE 128  // Maximum number of open files of a process.

typedef struct dinode InodeEntry;
typedef struct super_block SuperBlock;
typedef u16 InodeType;
typedef struct indirect_block IndirectBlock;
typedef struct dirent DirEntry;
typedef struct log_header LogHeader;