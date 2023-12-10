#include <driver/sddef.h>

/* Wait for interrupt. Return after interrupt handling. */
static int sd_wait_for_interrupt(unsigned int mask);

/* Data synchronization barrier. Use before access memory. */
static ALWAYS_INLINE void arch_dsb_sy();

/* Call handler when interrupt. */
void set_interrupt_handler(InterruptType type, InterruptHandler handler);

/* LBA for super block*/
usize block_no_sb = 0;

static Queue bufs;

SpinLock sd_lock;

ALWAYS_INLINE u32 get_EMMC_DATA() {
    return *EMMC_DATA;
}

ALWAYS_INLINE u32 get_and_clear_EMMC_INTERRUPT() {
    u32 t = *EMMC_INTERRUPT;
    *EMMC_INTERRUPT = t;
    return t;
}

/* 
 * Initialize SD card and parse MBR.
 *
 * 1. The first partition should be FAT and is used for booting.
 * 2. The second partition is used by our file system. 
 *
 * 1. Call sdInit.
 * 2. Initialize the lock and request queue if any.
 * 3. Set interrupt handler for IRQ_SDIO,IRQ_ARASANSDIO
 * 4. Read and parse 1st block (MBR) and collect whatever information you want.
 */
void sd_init() {
    sd_launch();
    init_spinlock(&sd_lock);
    queue_init(&bufs);

    set_interrupt_handler(IRQ_SDIO, sd_intr);
    set_interrupt_handler(IRQ_ARASANSDIO, sd_intr);

    Buf mbr_buf;
    sdrw(&mbr_buf);
    block_no_sb = *(u32*)(mbr_buf.data + 0x1ce + 0x8);
    printk("\n\nLBA of first absolute sector in the partition: %d.\nNumber of sectors in partition: %d\n\n", *(u32*)(mbr_buf.data + 0x1ce + 0x8), *(u32*)(mbr_buf.data + 0x1ce + 0xc));
}

/* 
 * Start the request for b. Caller must hold sdlock. 
 * Address is different depending on the card type.
 * HC pass address as block #.
 * SC pass address straight through. 
 */
void sd_start(Buf* b) {
    int bno =
        SDCard.type == SD_TYPE_2_HC ? (int)b->blockno : (int)b->blockno << 9;
    int write = b->flags & B_DIRTY;

    arch_dsb_sy();

    // Ensure that any data operation has completed before doing the transfer.
    if (*EMMC_INTERRUPT) {
        printk("emmc interrupt flag should be empty: 0x%x. \n",
               *EMMC_INTERRUPT);
        PANIC();
    }
    arch_dsb_sy();

    // Work out the status, interrupt and command values for the transfer.
    int cmd = write ? IX_WRITE_SINGLE : IX_READ_SINGLE;

    int resp;
    *EMMC_BLKSIZECNT = 512;

    if ((resp = sd_send_command_arg(cmd, bno))) {
        printk("* EMMC send command error.\n");
        PANIC();
    }

    int done = 0;
    u32* intbuf = (u32*)b->data;
    if (!(((i64)b->data) & 0x03) == 0) {
        printk("Only support word-aligned buffers. \n");
        PANIC();
    }

    if (write) {
        // Wait for ready interrupt for the next block.
        if ((resp = sd_wait_for_interrupt(INT_WRITE_RDY))) {
            printk("* EMMC ERROR: Timeout waiting for ready to write\n");
            PANIC();
        }
        if (*EMMC_INTERRUPT) {
            printk("%d\n", *EMMC_INTERRUPT);
            PANIC();
        }
        while (done < 128)
            *EMMC_DATA = intbuf[done++];
    }
}

/* 
 * The interrupt handler. Sync buf with disk.
 *
 * Pay attention to whether there is any element in the buflist.
 * Understand the meanings of EMMC_INTERRUPT, EMMC_DATA, INT_DATA_DONE,
 * INT_READ_RDY, B_DIRTY, B_VALID and some other flags.
 *
 * Notice that reading and writing are different, you can use flags
 * to identify.
 *
 * If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
 * Else if B_VALID is not set, read buf from disk, set B_VALID.
 *
 * Remember to clear the flags after reading/writing.
 *
 * When finished, remember to use pop and check whether the list is
 * empty, if not, continue to read/write.
 *
 * You may use some buflist functions, arch_dsb_sy(), sd_start(), post_sem()
 * and sdWaitForInterrupt() to complete this function.
 */
void sd_intr() {
    queue_lock(&bufs);
    Buf* b = container_of(queue_front(&bufs), Buf, bq_node);
    int flag = b->flags;
    u32* intbuf = (u32*)b->data;
    int code = 0;
    if (flag == 0) {
        if ((code = sd_wait_for_interrupt(INT_READ_RDY))) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }

        for (int i = 0; i < 128; ++i)
            intbuf[i] = get_EMMC_DATA();

        if ((code = sd_wait_for_interrupt(INT_DATA_DONE))) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }

        b->flags = B_VALID;
    } else if (flag & B_DIRTY) {
        if (sd_wait_for_interrupt(INT_DATA_DONE)) {
            printk("\n[Error] SD operation with return code: %d\n", code);
            PANIC();
        }

        b->flags = B_VALID;
    } else {
        printk("\n[Error] Unknown buf flag.\n");
        PANIC();
    }
    queue_pop(&bufs);
    post_sem(&b->sem);
    
    if (queue_empty(&bufs) == false) {
        b = container_of(queue_front(&bufs), Buf, bq_node); 
        _acquire_spinlock(&sd_lock);
        sd_start(b);
        _release_spinlock(&sd_lock);
    }

    queue_unlock(&bufs);
}

void sdrw(Buf* b) {
    init_sem(&(b->sem), 0);
    queue_lock(&bufs);
    queue_push(&bufs, &(b->bq_node));
    queue_unlock(&bufs);
    if (bufs.size == 1) {
        _acquire_spinlock(&sd_lock);
        sd_start(b);
        _release_spinlock(&sd_lock);
    }
    bool r = wait_sem(&(b->sem));
    (void) r;
}