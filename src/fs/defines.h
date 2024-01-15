#pragma once

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

#define BIT_PER_BLOCK (BLOCK_SIZE * 8)
#define BLOCK_SIZE 512
#define LOG_MAX_SIZE ((BLOCK_SIZE - sizeof(usize)) / sizeof(usize))
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
#define FILE_NAME_MAX_LENGTH                                                   \
    14 // The maximum length of file names, including trailing '\0'.
#define FSSIZE 1000 // Size of file system in blocks
#define NFILE 65536 // Maximum number of open files in the whole system.

struct super_block {
    u32 num_blocks; // total number of blocks in filesystem.
    u32 num_data_blocks;
    u32 num_inodes;
    u32 num_log_blocks; // number of blocks for logging, including log header.
    u32 log_start;      // the first block of logging area.
    u32 inode_start;    // the first block of inode area.
    u32 bitmap_start;   // the first block of bitmap area.
};

struct dinode {
    InodeType type; // `type == INODE_INVALID` implies this inode is free.
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

/* Directory entry. `inode_no == 0` implies this entry is free. */
struct dirent {
    u16 inode_no;
    char name[FILE_NAME_MAX_LENGTH];
};

struct log_header {
    usize num_blocks;
    usize block_no[LOG_MAX_SIZE];
};

typedef struct dinode InodeEntry;
typedef struct super_block SuperBlock;
typedef u16 InodeType;
typedef struct indirect_block IndirectBlock;
typedef struct dirent DirEntry;
typedef struct log_header LogHeader;