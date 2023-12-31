#include <aarch64/intrinsic.h>
#include <driver/sd.h>

/* Base point for super block. */
usize block_no_sb = 0;

static Queue bufs;

SpinLock sd_lock;

/* Initialize SD card and parse MBR. */
void 
disk_init() 
{
    sd_init();
    init_spinlock(&sd_lock);
    queue_init(&bufs);

    set_interrupt_handler(IRQ_SDIO, disk_intr);
    set_interrupt_handler(IRQ_ARASANSDIO, disk_intr);

    Buf mbr_buf;
    disk_rw(&mbr_buf);

    // Set the base LBA of the super block
    block_no_sb = *(u32 *)(mbr_buf.data + 0x1ce + 0x8);
}

/* Start the request for b. Caller must hold sdlock. */
void 
disk_start(Buf *b) 
{
    // Determine the SD card type and the corresponding address style.
    // HC pass addresses as block numbers.
    // SC pass addresses straight through.
    int bno = (sdd.type == SD_TYPE_2_HC) ? (int)b->blockno : (int)b->blockno << 9;
    int write = b->flags & B_DIRTY;
    u32 *intbuf = (u32 *)b->data;
    int cmd = write ? IX_WRITE_SINGLE : IX_READ_SINGLE;
    int i = 0;

    // Ensure that any data operation has completed before doing the transfer.
    arch_dsb_sy(); if (*EMMC_INTERRUPT) PANIC();

    arch_dsb_sy(); *EMMC_BLKSIZECNT = 512;

    if ((sd_send_command_arg(cmd, bno))) PANIC();

    if (((i64)b->data & 0x03) != 0) PANIC();

    if (write) {
        // Wait for ready interrupt for the next block.
        if ((sd_wait_for_interrupt(INT_WRITE_RDY))) PANIC();

        arch_dsb_sy(); if (*EMMC_INTERRUPT) PANIC();

        while (i < 128) {
            arch_dsb_sy(); *EMMC_DATA = intbuf[i];
            i++;
        }
    }
}

/* The interrupt handler. Sync buf with disk. */
void 
disk_intr() 
{
    Buf *b = container_of(queue_front(&bufs), Buf, bq_node);
    u32 *intbuf = (u32 *)b->data;
    int flags = b->flags;

    queue_lock(&bufs);

    if (flags == 0) {
        if (sd_wait_for_interrupt(INT_READ_RDY)) PANIC();

        for (usize i = 0; i < 128; i++) {
            arch_dsb_sy();
            intbuf[i] = *EMMC_DATA;
        }

        if (sd_wait_for_interrupt(INT_DATA_DONE)) PANIC();
    } else if (flags & B_DIRTY) {
        if (sd_wait_for_interrupt(INT_DATA_DONE)) PANIC();
    } else {
        PANIC();
    }

    b->flags = B_VALID;

    queue_pop(&bufs);
    post_sem(&b->sem);

    if (!queue_empty(&bufs)) {
        b = container_of(queue_front(&bufs), Buf, bq_node);
        disk_start(b);
    }

    queue_unlock(&bufs);
}

void 
disk_rw(Buf *b) 
{
    init_sem(&(b->sem), 0);
    queue_lock(&bufs);
    queue_push(&bufs, &(b->bq_node));
    if (bufs.size == 1) disk_start(b);
    queue_unlock(&bufs);
    unalertable_wait_sem(&(b->sem));
}