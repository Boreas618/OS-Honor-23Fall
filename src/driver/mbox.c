#include <driver/base.h>
#include <driver/mbox.h>

#include <aarch64/intrinsic.h>
#include <aarch64/mmu.h>
#include <driver/memlayout.h>
#include <lib/defines.h>
#include <lib/printk.h>

/* Define mailbox constants related to VideoCore communication on Raspberry Pi.
 */
#define VIDEOCORE_MBOX (MMIO_BASE + 0x0000B880)
#define MBOX_READ ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x00))
#define MBOX_POLL ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x10))
#define MBOX_SENDER ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x14))
#define MBOX_STATUS ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x18))
#define MBOX_CONFIG ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x1C))
#define MBOX_WRITE ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x20))
#define MBOX_RESPONSE 0x80000000
#define MBOX_FULL 0x80000000
#define MBOX_EMPTY 0x40000000

/* Define mailbox tags for various operations. */
#define MBOX_TAG_GET_ARM_MEMORY 0x00010005
#define MBOX_TAG_GET_CLOCK_RATE 0x00030002
#define MBOX_TAG_END 0x0
#define MBOX_TAG_REQUEST

/* Reads data from the mailbox */
int mbox_read(u8 chan) {
    while (1) {
        arch_dsb_sy();
        while (*MBOX_STATUS & MBOX_EMPTY) // Wait while mailbox is empty
            ;
        arch_dsb_sy();
        u32 r = *MBOX_READ;
        if ((r & 0xF) ==
            chan) { // Check if the read channel matches requested channel
            return (i32)(r >> 4); // Return the read value
        }
    }
    arch_dsb_sy();
    return 0;
}

/* Writes data to the mailbox. */
void mbox_write(u32 buf, u8 chan) {
    arch_dsb_sy();
    if (!((buf & 0xF) == 0 && (chan & ~0xF) == 0)) {
        PANIC();
    }
    while (*MBOX_STATUS & MBOX_FULL)
        ;
    arch_dsb_sy();
    *MBOX_WRITE = (buf & ~0xFu) | chan;
    arch_dsb_sy();
}

/* Retrieves the size of the ARM memory. */
int mbox_get_arm_memory() {
    __attribute__((aligned(16)))
    u32 buf[] = {36, 0, MBOX_TAG_GET_ARM_MEMORY, 8, 0, 0, 0, MBOX_TAG_END};
    if (!((K2P(buf) & 0xF) == 0)) {
        printk("Buffer should align to 16 bytes. \n");
        PANIC();
    }
    /* Following lines handle sending and receiving data through the mailbox/ */
    arch_dsb_sy();
    arch_dccivac(buf, sizeof(buf));
    arch_dsb_sy();
    mbox_write((u32)K2P(buf), 8);
    arch_dsb_sy();
    mbox_read(8);
    arch_dsb_sy();
    arch_dccivac(buf, sizeof(buf));
    arch_dsb_sy();

    if (buf[5] != 0) {
        printk("Memory base address should be zero. \n");
        PANIC();
    }

    if (buf[6] == 0) {
        printk("Memory size should not be zero. \n");
        PANIC();
    }
    return (int)buf[6];
}

/* Get the clock rate. */
int mbox_get_clock_rate() {
    __attribute__((aligned(16)))
    u32 buf[] = {36, 0, MBOX_TAG_GET_CLOCK_RATE, 8, 0, 1, 0, MBOX_TAG_END};
    if (!((K2P(buf) & 0xF) == 0)) {
        printk("Buffer should align to 16 bytes. \n");
        PANIC();
    }
    arch_dsb_sy();
    arch_dccivac(buf, sizeof(buf));
    arch_dsb_sy();
    mbox_write((u32)K2P(buf), 8);
    arch_dsb_sy();
    mbox_read(8);
    arch_dsb_sy();
    arch_dccivac(buf, sizeof(buf));
    arch_dsb_sy();

    return (int)buf[6];
}
