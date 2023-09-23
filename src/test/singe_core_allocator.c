#include <aarch64/intrinsic.h>
#include <common/rc.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <test/test.h>

extern RefCount alloc_page_cnt;

void sing_alloc_test()
{
    int i = cpuid();
    //int r = alloc_page_cnt.count;
    printk("Single Core Test\nCPU id is %d\n", i);
    u64* addr_1 = kalloc(63);
    u64* addr_2 = kalloc(31);
    u64* addr_3 = kalloc(62);
    printk("%d\n", (int)(u64)addr_1);
    printk("%d\n", (int)(u64)addr_2);
    printk("%d\n", (int)(u64)addr_3);
}