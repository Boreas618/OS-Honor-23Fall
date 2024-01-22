#pragma once

#include <proc/proc.h>
#include <kernel/param.h>

#define activate_proc(proc) _activate_proc(proc, false)
#define alert_proc(proc) _activate_proc(proc, true)
#define yield() (schedule(RUNNABLE))

extern u64 proc_entry();
bool _activate_proc(struct proc *, bool onalert);
void schedule(enum procstate new_state);
WARN_RESULT struct proc *thisproc();
void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
void trap_return();
