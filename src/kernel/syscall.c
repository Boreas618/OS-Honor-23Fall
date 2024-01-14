#include <vm/paging.h>
#include <lib/printk.h>
#include <vm/pt.h>
#include <proc/sched.h>
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

// check if the virtual address [start,start+size) is READABLE by the current
// user process
bool user_readable(const void *start, usize size) {
    // TODO
    (void) start;
    (void) size;
    return 0;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by
// the current user process
bool user_writeable(const void *start, usize size) {
    // TODO
    (void) start;
    (void) size;
    return 0;
}

// get the length of a string including tailing '\0' in the memory space of
// current user process return 0 if the length exceeds maxlen or the string is
// not readable by the current user process
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