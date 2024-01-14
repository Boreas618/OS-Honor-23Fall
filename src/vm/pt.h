#pragma once

#include <aarch64/mmu.h>
#include <lib/list.h>

struct vmspace {
    PTEntry *pgtbl;
    struct spinlock lock;
    struct list vmregions;
};

void init_vmspace(struct vmspace *vms);
WARN_RESULT PTEntry *get_pte(struct vmspace *vms, u64 va, bool alloc);
void vmmap(struct vmspace *pd, u64 va, void *ka, u64 flags);
void free_vmspace(struct vmspace *vms);
void attach_vmspace(struct vmspace *vms);
int copyout(struct vmspace *pd, void *va, void *p, usize len);