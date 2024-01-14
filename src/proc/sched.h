#pragma once

#include <proc/proc.h>

extern u64 proc_entry();

#define activate_proc(proc) _activate_proc(proc, false)

#define alert_proc(proc) _activate_proc(proc, true)

/* Call lock_for_sched() before sched() */
#define sched(checker, new_state) (checker_end_ctx(checker), _sched(new_state))

#define yield() (_sched(RUNNABLE))

bool _activate_proc(struct proc*, bool onalert);

void _sched(enum procstate new_state);

WARN_RESULT struct proc* thisproc();

void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

void trap_return();
