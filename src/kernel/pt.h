#pragma once

#include <aarch64/mmu.h>

struct pgdir
{
    PTEntry* pt;
};

void init_pgdir(struct pgdir* pgdir);
WARN_RESULT PTEntry* get_pte(struct pgdir* pgdir, u64 va, bool alloc);
void free_pgdir(struct pgdir* pgdir);
void attach_pgdir(struct pgdir* pgdir);