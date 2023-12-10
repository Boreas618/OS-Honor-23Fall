#include <fs/defines.h>

/**
 * struct BlockDevice - Interface for block devices.
 * @read:  Function pointer to read BLOCK_SIZE bytes from a block.
 * @write: Function pointer to write BLOCK_SIZE bytes to a block.
 *
 * This structure represents a block device with basic read and write
 * operations. The read function reads BLOCK_SIZE bytes from a block
 * into a buffer, while the write function writes BLOCK_SIZE bytes from
 * a buffer to a block.
 */
struct BlockDevice {
    void (*read)(usize block_no, u8 *buffer);
    void (*write)(usize block_no, u8 *buffer);
};

extern struct BlockDevice block_device;

void init_block_device(void);
const struct SuperBlock *get_super_block(void);