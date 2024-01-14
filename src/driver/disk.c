#include <aarch64/intrinsic.h>
#include <driver/sd.h>

/* Base point for super block. */
usize sb_base = 0;

static struct queue bufs;

struct spinlock disk_lock;

/* Initialize SD card and parse MBR. */
void disk_init() {
    sd_init();
    init_spinlock(&disk_lock);
    queue_init(&bufs);

    set_interrupt_handler(IRQ_SDIO, disk_intr);
    set_interrupt_handler(IRQ_ARASANSDIO, disk_intr);

    // First read MBR to obtain some metadata.
    struct buf mbr_buf;
    disk_rw(&mbr_buf);

    // Set the base LBA of the super block.
    sb_base = *(u32 *)(mbr_buf.data + 0x1ce + 0x8);
}

/* Start the request for b. Caller must hold sdlock. */
void disk_start(struct buf *b) {
    // Determine the SD card type and the corresponding address style.
    // HC pass addresses as block numbers.
    // SC pass addresses straight through.
    int bno =
        (sdd.type == SD_TYPE_2_HC) ? (int)b->blockno : (int)b->blockno << 9;
    int write = b->flags & B_DIRTY;
    u32 *intbuf = (u32 *)b->data;
    int cmd = write ? IX_WRITE_SINGLE : IX_READ_SINGLE;

    // Ensure that any data operation has completed before doing the transfer.
    if (*EMMC_INTERRUPT) 
        PANIC();
    arch_dsb_sy();

    // Set the block size.
    *EMMC_BLKSIZECNT = 512;
    arch_dsb_sy();

    // Send the command and the associated block number.
    if (sd_send_command_arg(cmd, bno))
        PANIC();
    arch_dsb_sy();

    // Check the result.
    if (((i64)b->data & 0x03) != 0)
        PANIC();
    arch_dsb_sy();

    if (write) {
        // Wait for ready interrupt for the next block.
        if ((sd_wait_for_interrupt(INT_WRITE_RDY)))
            PANIC();
        arch_dsb_sy();
        
        // Ensure that any data operation has completed before doing the transfer.
        if (*EMMC_INTERRUPT)
            PANIC();
        arch_dsb_sy();

        // Write the data in 4-byte units.
        for (int i = 0; i < 128; i++) {
            *EMMC_DATA = intbuf[i];
            arch_dsb_sy();
        }
    }
}

/* The interrupt handler. Sync buf with disk. */
void disk_intr() {
    _acquire_spinlock(&disk_lock);
    struct buf *b = container_of(queue_front(&bufs), struct buf, bq_node);
    u32 *intbuf = (u32 *)b->data;
    int flags = b->flags;

    if (flags == 0) {
        // Gets the ready signal and starts reading.
        if (sd_wait_for_interrupt(INT_READ_RDY))
            PANIC();
        // Reads the data.
        for (usize i = 0; i < 128; i++) {
            arch_dsb_sy();
            intbuf[i] = *EMMC_DATA;
        }
        // Reading has finished.
        if (sd_wait_for_interrupt(INT_DATA_DONE))
            PANIC();
        goto wrap_up;
    }

    if (flags & B_DIRTY) {
        // Writing has finished.
        if (sd_wait_for_interrupt(INT_DATA_DONE))
            PANIC();
        goto wrap_up;
    }

    PANIC();

wrap_up:
    // Set the flag back to B_VALID.
    b->flags = B_VALID;
    queue_pop(&bufs);
    // Wake up the disk_rw task.
    post_sem(&b->sem);
    // Turn to the next buf.
    if (!queue_empty(&bufs)) {
        b = container_of(queue_front(&bufs), struct buf, bq_node);
        disk_start(b);
    }
    _release_spinlock(&disk_lock);
}

void disk_rw(struct buf *b) {
    init_sem(&(b->sem), 0);
    acquire_spinlock(&disk_lock);
    queue_push(&bufs, &(b->bq_node));
    if (bufs.size == 1)
        disk_start(b);
    _lock_sem(&b->sem);
    release_spinlock(&disk_lock);
    ASSERT(_wait_sem(&b->sem, false));
}