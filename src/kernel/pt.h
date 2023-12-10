#pragma once

#include <aarch64/mmu.h>
#include <common/list.h>

struct pgdir {
    PTEntry* pt;
    SpinLock lock;
    ListNode section_head;
};

void init_pgdir(struct pgdir *pgdir);
WARN_RESULT PTEntry* get_pte(struct pgdir *pgdir, u64 va, bool alloc);
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags);
void free_pgdir(struct pgdir *pgdir);
void attach_pgdir(struct pgdir *pgdir);