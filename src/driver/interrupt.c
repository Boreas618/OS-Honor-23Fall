#include <aarch64/intrinsic.h>
#include <driver/base.h>
#include <driver/clock.h>
#include <driver/interrupt.h>
#include <driver/irq.h>
#include <kernel/init.h>
#include <lib/printk.h>
#include <proc/sched.h>

static InterruptHandler int_handler[NUM_IRQ_TYPES];

define_early_init(interrupt)
{
	for (usize i = 0; i < NUM_IRQ_TYPES; i++)
		int_handler[i] = NULL;
	device_put_u32(GPU_INT_ROUTE, GPU_IRQ2CORE(0));
}

void set_interrupt_handler(InterruptType type, InterruptHandler handler)
{
	device_put_u32(ENABLE_IRQS_1 + 4 * (type / 32), 1u << (type % 32));
	int_handler[type] = handler;
}

void interrupt_global_handler()
{
	u32 source = device_get_u32(IRQ_SRC_CORE(cpuid()));

	/* Handle Non-secure Physical Timer interrupts */
	if (source & IRQ_SRC_CNTPNSIRQ) {
		source ^= IRQ_SRC_CNTPNSIRQ;
		invoke_clock_handler();
	}

	/* Handle GPU interrupts */
	if (source & IRQ_SRC_GPU) {
		source ^= IRQ_SRC_GPU;

		/* Aggregate pending GPU interrupts from both registers */
		u64 map = device_get_u32(IRQ_PENDING_1) |
			  (((u64)device_get_u32(IRQ_PENDING_2)) << 32);

		/* Process each interrupt type */
		for (usize i = 0; i < NUM_IRQ_TYPES; i++) {
			if ((map >> i) & 1) { // Check if interrupt is pending
				if (int_handler[i]) {
					int_handler[i](); // Call the registered handler
				} else {
					printk("Unknown interrupt type %lld",
					       i);
					PANIC();
				}
			}
		}
	}

	/* Check for any unhandled interrupt sources */
	if (source != 0) {
		printk("Unknown interrupt sources: %x", source);
		PANIC();
	}
}
