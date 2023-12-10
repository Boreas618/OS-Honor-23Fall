#include <kernel/init.h>
#include <driver/sd.h>
#include <fs/block_device.h>
#include <kernel/printk.h>

extern usize block_no_sb;

/*
 * A simple implementation of reading a block from SD card.
 * 
 * `block_no` is the block number to read
 * `buffer` is the buffer to store the data
 */
static void sd_read(usize block_no, u8 *buffer) {
    struct buf b;
    b.blockno = (u32)block_no;
    b.flags = 0;
    sdrw(&b);
    memcpy(buffer, b.data, BLOCK_SIZE);
}

/*
 * A simple implementation of writing a block to SD card.
 * 
 * `block_no` is the block number to write
 * `buffer` is the buffer to store the data
 */
static void sd_write(usize block_no, u8 *buffer) {
    struct buf b;
    b.blockno = (u32)block_no;
    b.flags = B_DIRTY | B_VALID;
    memcpy(b.data, buffer, BLOCK_SIZE);
    sdrw(&b);
}

/*
 * The in-memory copy of the super block.
 * 
 * We may need to read the super block multiple times, so keep a copy of it in memory.
 * 
 * The super block, in our lab, is always read-only, so we don't need to write it back.
 */
static u8 sblock_data[BLOCK_SIZE];

BlockDevice block_device;

void init_block_device() {
    // Initialize the block device
    block_device.read = sd_read;
    block_device.write = sd_write;

    block_device.read(block_no_sb, (u8*)sblock_data);

    const SuperBlock* sb = get_super_block();
	printk("num_blocks: %d\n",sb->num_blocks);
	printk("num_data_blocks: %d\n", sb->num_data_blocks);
	printk("num_inodes: %d\n", sb->num_inodes);
	printk("num_log_blocks: %d\n", sb->num_log_blocks);
	printk("log_start: %d\n", sb->log_start);
	printk("inode_start: %d\n", sb->inode_start);
	printk("bitmap_start: %d\n", sb->bitmap_start);
}

const SuperBlock *get_super_block() { return (const SuperBlock *)sblock_data; }