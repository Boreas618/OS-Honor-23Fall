#pragma once

#include <aarch64/mmu.h>
#include <lib/list.h>

#define PT_FREEZE 1
#define PT_UNFREEZE 1 << 1

struct vmspace {
	pgtbl_entry_t *pgtbl;
	struct spinlock lock;
	struct list vmregions;
};

WARN_RESULT pgtbl_entry_t *get_pte(pgtbl_entry_t *pg, u64 va, bool alloc);
void map_in_pgtbl(pgtbl_entry_t *pt, u64 va, void *ka, u64 flags);
void unmap_in_pgtbl(pgtbl_entry_t *pt, u64 va);
void free_page_table(pgtbl_entry_t **pt);
void set_page_table(pgtbl_entry_t *pt);
void unmap_range_in_pgtbl(pgtbl_entry_t *pt, u64 begin, u64 end);
void modify_pgtbl(pgtbl_entry_t *pt, int flag);
int copy_to_user(pgtbl_entry_t *pd, void *va, void *p, usize len);
void print_pgtbl(pgtbl_entry_t *pt);