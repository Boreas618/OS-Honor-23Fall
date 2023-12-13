#pragma once

#include <aarch64/mmu.h>
#include <lib/list.h>

struct vmspace {
    PTEntry *pt;
    SpinLock lock;
    ListNode section_head;
};

void init_pgdir(struct vmspace *pgdir);
WARN_RESULT PTEntry *get_pte(struct vmspace *pgdir, u64 va, bool alloc);
void vmmap(struct vmspace *pd, u64 va, void *ka, u64 flags);
void free_pgdir(struct vmspace *pgdir);
void attach_pgdir(struct vmspace *pgdir);