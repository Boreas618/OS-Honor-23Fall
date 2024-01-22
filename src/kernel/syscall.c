#include <kernel/syscall.h>
#include <lib/printk.h>
#include <lib/sem.h>
#include <proc/sched.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>

void *syscall_table[NR_SYSCALL];

void syscall_entry(struct uctx *ctx)
{
	u64 id, arg0, arg1, arg2, arg3, arg4, arg5;
	id = ctx->regs[8];
	arg0 = ctx->regs[0];
	arg1 = ctx->regs[1];
	arg2 = ctx->regs[2];
	arg3 = ctx->regs[3];
	arg4 = ctx->regs[4];
	arg5 = ctx->regs[5];

	if (id >= NR_SYSCALL || syscall_table[id] == NULL) {
		printk("[FDCore] System call %lld not found.\n", id);
		return;
	}

	ctx->regs[0] =
		((u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
			arg0, arg1, arg2, arg3, arg4, arg5);
}