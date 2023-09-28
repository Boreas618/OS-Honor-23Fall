#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>

#define PAGE_PARTITIONED 1;

void* kalloc_page();
void kfree_page(void*);

void* kalloc(isize);
void kfree(void*);

typedef struct partitioned_page_node {
    struct ListNode *prev, *next;
    struct page* page;
    bool head;
    u8 bucket_index;
} PartitionedPageNode;

typedef struct page {
    u64 addr;
    u32 base_size;
    u8 flag;
    u32 free_head;
    u32 alloc_partitions_cnt;
    PartitionedPageNode partitioned_node;
} Page;

typedef struct recycle_entity {
    struct ListNode *prev, *next;
    bool head;
    u8 bucket_index;
    u64 addr;
} RecycleNode;