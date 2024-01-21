#include <kernel/syscall.h>
#include <lib/printk.h>
#include <lib/sem.h>
#include <proc/sched.h>
#include <vm/paging.h>
#include <vm/pt.h>

void *syscall_table[NR_SYSCALL];

void syscall_entry(UserContext *context) {
    u64 id = context->regs[8];
    u64 arg0 = context->regs[0];
    u64 arg1 = context->regs[1];
    u64 arg2 = context->regs[2];
    u64 arg3 = context->regs[3];
    u64 arg4 = context->regs[4];
    u64 arg5 = context->regs[5];

    if ((id < NR_SYSCALL) && (syscall_table[id] != NULL)) {
        context->regs[0] =
            ((u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
                arg0, arg1, arg2, arg3, arg4, arg5);
    }
}

/**
 * Check if the virtual address [start,start+size) is READABLE by the current
 * user process
 */
bool user_readable(const void *start, usize size) {
    struct list vmregions = thisproc()->vmspace.vmregions;
    list_forall(p, vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if (v->begin <= (u64)start && ((u64)start) + size <= v->end)
            return true;
    }
    return false;
}

/* Check if the virtual address [start,start+size) is READABLE & WRITEABLE by
 * the current user process. */
bool user_writeable(const void *start, usize size) {
    struct list vmregions = thisproc()->vmspace.vmregions;
    list_forall(p, vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if (!(v->flags & ST_RO) && v->begin <= (u64)start &&
            ((u64)start) + size <= v->end)
            return true;
    }
    return false;
}

/* Get the length of a string including tailing '\0' in the memory space of
 * current user process return 0 if the length exceeds maxlen or the string is
 * not readable by the current user process */
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}