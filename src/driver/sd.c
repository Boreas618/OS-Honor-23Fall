#include <aarch64/intrinsic.h>
#include <driver/sddef.h>

/* Wait for interrupt. Return after interrupt handling. */
static int sd_wait_for_interrupt(unsigned int mask);

/* Data synchronization barrier. Use before access memory. */
static ALWAYS_INLINE void arch_dsb_sy();

/* Call handler when interrupt. */
void set_interrupt_handler(InterruptType type, InterruptHandler handler);

/* Base point for super block. */
usize block_no_sb = 0;

static Queue bufs;

SpinLock sd_lock;

/* Initialize SD card and parse MBR. */
void sd_init() {
    sd_launch();
    init_spinlock(&sd_lock);
    queue_init(&bufs);

    set_interrupt_handler(IRQ_SDIO, sd_intr);
    set_interrupt_handler(IRQ_ARASANSDIO, sd_intr);

    Buf mbr_buf;
    sdrw(&mbr_buf);

    // Set the base LBA of the super block
    block_no_sb = *(u32 *)(mbr_buf.data + 0x1ce + 0x8);
}

/* Start the request for b. Caller must hold sdlock. */
void sd_start(Buf *b)
{
    int bno, write, cmd, resp, done;
    u32 *intbuf;

    /* Determine the SD card type and the corresponding address style.
     * HC pass addresses as block numbers.
     * SC pass addresses straight through.
     */
    bno = (SDCard.type == SD_TYPE_2_HC) ? (int)b->blockno
                                        : (int)b->blockno << 9;
    write = b->flags & B_DIRTY;

    arch_dsb_sy();

    /* Ensure that any data operation has completed before doing the transfer. */
    if (*EMMC_INTERRUPT) {
        printk("emmc interrupt flag should be empty: 0x%x.\n", *EMMC_INTERRUPT);
        PANIC();
    }

    /* Work out the status, interrupt, and command values for the transfer. */
    cmd = write ? IX_WRITE_SINGLE : IX_READ_SINGLE;

    arch_dsb_sy();
    *EMMC_BLKSIZECNT = 512;

    if ((resp = sd_send_command_arg(cmd, bno))) {
        printk("* EMMC send command error.\n");
        PANIC();
    }

    done = 0;
    intbuf = (u32 *)b->data;
    if (((i64)b->data & 0x03) != 0) {
        printk("Only support word-aligned buffers.\n");
        PANIC();
    }

    if (write) {
        /* Wait for ready interrupt for the next block. */
        if ((resp = sd_wait_for_interrupt(INT_WRITE_RDY))) {
            printk("* EMMC ERROR: Timeout waiting for ready to write\n");
            PANIC();
        }

        arch_dsb_sy();
        if (*EMMC_INTERRUPT) {
            printk("%d\n", *EMMC_INTERRUPT);
            PANIC();
        }

        while (done < 128)
            *EMMC_DATA = intbuf[done++];
    }
}

/* The interrupt handler. Sync buf with disk. */
void sd_intr()
{
    Buf *b;
    u32 *intbuf;
    int flags, code, i;

    queue_lock(&bufs);

    b = container_of(queue_front(&bufs), Buf, bq_node);
    flags = b->flags;
    intbuf = (u32 *)b->data;
    code = 0;

    if (flags == 0) {
        if ((code = sd_wait_for_interrupt(INT_READ_RDY))) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }

        for (i = 0; i < 128; ++i) {
            arch_dsb_sy();
            intbuf[i] = *EMMC_DATA;
        }

        if ((code = sd_wait_for_interrupt(INT_DATA_DONE))) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }
    } else if (flags & B_DIRTY) {
        if (sd_wait_for_interrupt(INT_DATA_DONE)) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }
    } else {
        printk("\n[Error] Unknown buf flag.\n");
        PANIC();
    }

    b->flags = B_VALID;

    queue_pop(&bufs);
    post_sem(&b->sem);

    if (!queue_empty(&bufs)) {
        b = container_of(queue_front(&bufs), Buf, bq_node);
        _acquire_spinlock(&sd_lock);
        sd_start(b);
        _release_spinlock(&sd_lock);
    }

    queue_unlock(&bufs);
}


void sdrw(Buf *b) {
    init_sem(&(b->sem), 0);
    _acquire_spinlock(&sd_lock);
    queue_push(&bufs, &(b->bq_node));
    if (bufs.size == 1)
        sd_start(b);
    _release_spinlock(&sd_lock);
    unalertable_wait_sem(&(b->sem));
}