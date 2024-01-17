#pragma once

#include <aarch64/mmu.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/rc.h>

#define MAX_BUCKETS 16
#define MAX_PAGES 1048576
#define PAGE_COUNT ((P2K(PHYSTOP) - PAGE_BASE((u64) & end)) / PAGE_SIZE - 1)
#define VA2ID(vaddr) ((vaddr - PAGE_BASE((u64) & end) - PAGE_SIZE) / PAGE_SIZE)

struct partitioned_node {
    ListNode pp_node;
    struct page *page;
    u8 bucket_index;
};

struct page {
    u64 addr;
    u32 base_size;
    u64 free_head;
    u32 alloc_partitions_cnt;
    struct partitioned_node partitioned_node;
    RefCount ref;
};

typedef struct page Page;
typedef struct partitioned_node PartitionedNode;

u64 left_page_cnt(void);
WARN_RESULT void *get_zero_page(void);
u16 __round_up(isize s, u32 *rounded_size, u8 *bucket_index);
u64 __alloc_partition(Page *p);
PartitionedNode *__partition_page(u32 rounded_size, u8 bucket_index);
WARN_RESULT void *kalloc_page(void);
void kfree_page(void *page);
WARN_RESULT void *kalloc(isize size);
void kfree(void *ptr);
