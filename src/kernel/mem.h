#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>

#define _vaddr_to_id(vaddr) ((vaddr - PAGE_BASE((u64)&end) - PAGE_SIZE) / PAGE_SIZE);

WARN_RESULT void* kalloc_page();
void kfree_page(void*);

WARN_RESULT void* kalloc(isize);
void kfree(void*);

typedef struct partitioned_page_node {
    struct ListNode pp_node;
    struct page* page;
    u8 bucket_index;
} PartitionedPageNode;

typedef struct page {
    u64 addr;
    u32 base_size;
    u64 free_head;
    u32 alloc_partitions_cnt;
    PartitionedPageNode partitioned_node;
} Page;