#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/inode.h>
#include <common/defines.h>
#include <kernel/init.h>
#include <kernel/printk.h>

extern BlockDevice block_device;

define_rest_init(fs) {
    init_block_device();

    const SuperBlock* sblock = get_super_block();
    init_bcache(sblock, &block_device);
    init_inodes(sblock, &bcache);
}