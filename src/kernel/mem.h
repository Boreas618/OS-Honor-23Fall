#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>

#define PAGE_FRAGMENTED 1;
#define FRAGMENTS_FULL 1<<1;

void* kalloc_page();
void kfree_page(void*);

void* kalloc(isize);
void kfree(void*);

typedef struct fragment_node {
    struct ListNode *prev, *next;
    struct page* page;
} FragNode;

typedef struct page {
    u64 addr;
    u8 base_size;
    u8 flag;
    u32 candidate_idx;
    u32 alloc_fragments_cnt;
    FragNode frag_node;
} Page;