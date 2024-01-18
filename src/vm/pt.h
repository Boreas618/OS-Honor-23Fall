#pragma once

#include <aarch64/mmu.h>
#include <lib/list.h>

struct vmspace {
    pgtbl_entry_t *pgtbl;
    struct spinlock lock;
    struct list vmregions;
};

WARN_RESULT pgtbl_entry_t *get_pte(pgtbl_entry_t *pg, u64 va, bool alloc);
void map_range_in_pgtbl(struct vmspace *pd, u64 va, void *ka, u64 flags);
void free_page_table(pgtbl_entry_t **pt);
void set_page_table(pgtbl_entry_t* pt);
void free_range_in_pgtbl(u64 begin, u64 end);
void freeze_pgtbl(pgtbl_entry_t* pt);
int copyout(struct vmspace *pd, void *va, void *p, usize len);