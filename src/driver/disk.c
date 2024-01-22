#include <aarch64/intrinsic.h>
#include <driver/sd.h>
#include <lib/cond.h>

/* Base point for super block. */
usize sb_base = 0;

static struct queue bufs;

struct spinlock disk_lock;

/* Initialize disk and parse MBR. */
void disk_init()
{
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
void disk_start(struct buf *b)
{
	/**
     * High Capacity (HC) cards use block numbers directly, while Standard
     * Capacity (SC) cards use byte addresses, hence the shift operation (<< 9
     * effectively multiplies the block number by 512 to convert it to a byte
     * address).
     */
	int bno = (sdd.type == SD_TYPE_2_HC) ? (int)b->blockno :
					       (int)b->blockno << 9;
	int is_write = b->flags & B_DIRTY;
	u32 *intbuf = (u32 *)b->data;
	int cmd = is_write ? IX_WRITE_SINGLE : IX_READ_SINGLE;

	/**
     * Check if the data pointer is correctly aligned (it should be 4-byte
     * aligned for EMMC operations). If it's not aligned, a panic is triggered.
     */
	if (((i64)b->data & 0x03) != 0)
		PANIC();
	arch_dsb_sy();

	/* Ensure that any data operation has completed. */
	if (*EMMC_INTERRUPT)
		PANIC();
	arch_dsb_sy();

	/* Set the block size. */
	*EMMC_BLKSIZECNT = 512;
	arch_dsb_sy();

	/* Send the command and the associated block number. */
	if (sd_send_command_arg(cmd, bno))
		PANIC();
	arch_dsb_sy();

	if (is_write) {
		/* Wait for ready interrupt for the next block. */
		if ((sd_wait_for_interrupt(INT_WRITE_RDY)))
			PANIC();
		arch_dsb_sy();

		/* Ensure that any data operation has completed. */
		if (*EMMC_INTERRUPT)
			PANIC();
		arch_dsb_sy();

		/* Write the data in 4-byte units. */
		for (int i = 0; i < 128; i++) {
			*EMMC_DATA = intbuf[i];
			arch_dsb_sy();
		}
	}
}

/* The interrupt handler. Sync buf with disk. */
void disk_intr()
{
	acquire_spinlock(&disk_lock);
	struct buf *b = container_of(queue_front(&bufs), struct buf, bq_node);
	u32 *intbuf = (u32 *)b->data;
	int flags = b->flags;

	if (flags == 0) {
		/* Gets the ready signal and starts reading. */
		if (sd_wait_for_interrupt(INT_READ_RDY))
			PANIC();

		/* Reads the data. */
		for (usize i = 0; i < 128; i++) {
			arch_dsb_sy();
			intbuf[i] = *EMMC_DATA;
		}

		/* Reading has finished. */
		if (sd_wait_for_interrupt(INT_DATA_DONE))
			PANIC();
		goto wrap_up;
	} else if (flags & B_DIRTY) {
		/* Writing has finished. */
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
	release_spinlock(&disk_lock);
}

void disk_rw(struct buf *b)
{
	// Save the old flags for comparsion.
	int old_flags = b->flags;
	init_sem(&(b->sem), 0);
	acquire_spinlock(&disk_lock);

	// Push the buf into the queue. If the queue is empty, start the disk
	// operation.
	queue_push(&bufs, &(b->bq_node));
	if (bufs.size == 1)
		disk_start(b);
	// Loop until the flags has changed.
	while (old_flags == b->flags)
		cond_wait(&b->sem, &disk_lock);
	release_spinlock(&disk_lock);
}