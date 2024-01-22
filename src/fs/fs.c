#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/init.h>
#include <lib/defines.h>
#include <lib/printk.h>

define_rest_init(fs)
{
	// Initialize the block device.
	init_block_device();

	// Get the super block and initialize the block cache and inode.
	const struct super_block *sblock = get_super_block();
	init_bcache(sblock, &block_device);
	init_inodes(sblock, &bcache);
	init_ftable();
}