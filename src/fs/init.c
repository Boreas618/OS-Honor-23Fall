#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/inode.h>
#include <lib/defines.h>
#include <kernel/init.h>
#include <lib/printk.h>

extern BlockDevice block_device;

define_rest_init(fs) {
    init_block_device();

    const struct super_block* sblock = get_super_block();
    init_bcache(sblock, &block_device);
    init_inodes(sblock, &bcache);
}