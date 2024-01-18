#pragma once

#include <aarch64/mmu.h>
#include <lib/list.h>

struct vmspace {
    pgtbl_entry_t *pgtbl;
    struct spinlock lock;
    struct list vmregions;
};

void init_vmspace(struct vmspace *vms);
WARN_RESULT pgtbl_entry_t *get_pte(struct vmspace *vms, u64 va, bool alloc);
void vmmap(struct vmspace *pd, u64 va, void *ka, u64 flags);
void free_page_table(pgtbl_entry_t **pt);
void set_page_table(pgtbl_entry_t* pt);
int copyout(struct vmspace *pd, void *va, void *p, usize len);