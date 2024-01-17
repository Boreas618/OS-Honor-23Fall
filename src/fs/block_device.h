#pragma once

#include <lib/defines.h>
// #include <fs/defines.h>

#define BIT_PER_BLOCK (BLOCK_SIZE * 8)
#define BLOCK_SIZE 512
#define LOG_MAX_SIZE ((BLOCK_SIZE - sizeof(usize)) / sizeof(usize))

/**
 * block_device - interface for block devices.
 * @read:  Function pointer to read BLOCK_SIZE bytes from a block.
 * @write: Function pointer to write BLOCK_SIZE bytes to a block.
 *
 * This structure represents a block device with basic read and write
 * operations. The read function reads BLOCK_SIZE bytes from a block
 * into a buffer, while the write function writes BLOCK_SIZE bytes from
 * a buffer to a block.
 */
struct block_device {
    void (*read)(usize block_no, u8 *buffer);
    void (*write)(usize block_no, u8 *buffer);
};

struct super_block {
    u32 num_blocks; // total number of blocks in filesystem.
    u32 num_data_blocks;
    u32 num_inodes;
    u32 num_log_blocks; // number of blocks for logging, including log header.
    u32 log_start;      // the first block of logging area.
    u32 inode_start;    // the first block of inode area.
    u32 bitmap_start;   // the first block of bitmap area.
};

struct log_header {
    usize num_blocks;
    usize block_no[LOG_MAX_SIZE];
};

typedef struct block_device BlockDevice;
typedef struct dinode InodeEntry;
typedef struct super_block SuperBlock;

extern usize sb_base;
extern BlockDevice block_device;

void init_block_device(void);
const struct super_block *get_super_block(void);