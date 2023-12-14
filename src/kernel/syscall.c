#include <lib/printk.h>
#include <kernel/syscall.h>
#include <lib/sem.h>
#include <proc/sched.h>

void *syscall_table[NR_SYSCALL];

void syscall_entry(UserContext *context) {
    u64 id = context->gregs[8];

    u64 arg0 = context->gregs[0];
    u64 arg1 = context->gregs[1];
    u64 arg2 = context->gregs[2];
    u64 arg3 = context->gregs[3];
    u64 arg4 = context->gregs[4];
    u64 arg5 = context->gregs[5];

    (void)((id < NR_SYSCALL) && (syscall_table[id] != NULL) &&
           (context->gregs[0] =
                ((u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
                    arg0, arg1, arg2, arg3, arg4, arg5)));
}
