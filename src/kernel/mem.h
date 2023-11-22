#pragma once


#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>

typedef struct page Page;
typedef struct partitioned_page_node PartitionedPageNode;

typedef struct partitioned_page_node {
    struct ListNode pp_node;
    Page* page; // Use forward-declared type
    u8 bucket_index;
} PartitionedPageNode;

typedef struct page {
    u64 addr;
    u32 base_size;
    u64 free_head;
    u32 alloc_partitions_cnt;
    PartitionedPageNode partitioned_node;
} Page;

u16 _round_up(isize s, u32* rounded_size, u8* bucket_index);
u64 _alloc_partition(Page* p);
PartitionedPageNode* _partition_page(u32 rounded_size, u8 bucket_index);
WARN_RESULT void* kalloc_page();
void kfree_page(void* page);
WARN_RESULT void* kalloc(isize size);
void kfree(void* ptr);