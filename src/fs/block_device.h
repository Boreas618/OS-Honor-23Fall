#pragma once

#include <fs/defines.h>

/*
 * Interface for block devices.
 *
 * There is no OOP in C, but we can use function pointers to simulate it. 
 * This is a common pattern in C, and you can find it in Linux kernel too.
 */
typedef struct {
    /*
     * Read `BLOCK_SIZE` bytes in block at `block_no` to `buffer`.
     * 
     * Caller must guarantee `buffer` is large enough.
     * 
     * `block_no` is the block number to read from.
     * `buffer` is the buffer to read into.
     */
    void (*read)(usize block_no, u8 *buffer);

    /*
     * Write `BLOCK_SIZE` bytes in `buffer` to block at `block_no`.
     * 
     * Caller must guarantee `buffer` is large enough.
     * 
     * `block_no` the block number to write to.
     * `buffer` the buffer to write from.
     */
    void (*write)(usize block_no, u8 *buffer);
} BlockDevice;

/* The global block device instance. */
extern BlockDevice block_device;

/*
 * Initialize the block device.
 * 
 * This method must be called before any other block device methods,
 * and initializes the global block device and (if necessary) the
 * global super block.
 * 
 * e.g. for the SD card, this method is responsible for initializing
 * the SD card and reading the super block from the SD card.
 * 
 * You may want to put it into `*_init` method groups.
 */
void init_block_device();

/*
 * Get the global super block.
 *
 * const SuperBlock* is the global super block.
 */
const SuperBlock *get_super_block();