#include <aarch64/mmu.h>
#include <common/checker.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>

#include "aarch64/intrinsic.h"
#include "kernel/printk.h"

#define K_DEBUG 0

#define FAIL(...)        \
  {                      \
    printk(__VA_ARGS__); \
    while (1)            \
      ;                  \
  }

#define MAX_BUCKETS 16

#define MAX_PAGES 1048576

u16 _round_up(isize s, u32* rounded_size, u8* bucket_index);

u64 _vaddr_to_id(u64 vaddr);

u64 _alloc_partition(Page* p);

RefCount alloc_page_cnt;

PartitionedPageNode* _partition_page(u32 rounded_size, u8 bucket_index);

/** Memory is divided into pages. During the initialization process of the
   kernel, all the available pages are stored in the pages_free variable. Memory
   can be allocated in two ways: as a whole page or as a tiny partition. Pages
   have a fixed size of 4096 bytes, while partitions can range in size from 8 to
   2048 bytes.

   Why 8 to 2048? Because we need to maintain a linked list of free partitions
   and the pointer to the next free partition is stored in the former free
   partition. The partition should be at least 8 bytes in order to store a
   pointer. We choose 2048 as the maximum size of a partition because a
   partition with 4096 bytes is a whole page. */

/** The array for information about pages.*/
static Page page_info[MAX_PAGES];

/** The queue for all pages available. It is placed in BSS segment. */
static QueueNode* pages_free;

/** The hash map for pages with different maximum sizes of available area. */
static ListNode* partitioned_pages[MAX_BUCKETS];

/** The lock is used to prevent race condition in both kalloc and kfree. */
static SpinLock pages_lock;

/** The handle for accessing memory area that is ready for allocating.
   We can access to the area by referencing this handle.
   See linker.ld. */
extern char end[];

/** Initialization routine which initialize alloc_page_cnt to 0. */
define_early_init(alloc_page_cnt) { init_rc(&alloc_page_cnt); }

/** Initialization routine which starts tracking all of the pages. */
define_early_init(pages) {
  init_spinlock(&pages_lock);
  u64 inited_pages = 0;

  for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP);
       p += PAGE_SIZE) {
    add_to_queue(&pages_free, (QueueNode*)p);

    if (inited_pages >= MAX_PAGES) break;
    page_info[inited_pages].addr = p;
    page_info[inited_pages].base_size = 0;
    page_info[inited_pages].free_head = page_info[inited_pages].addr;
    page_info[inited_pages].alloc_partitions_cnt = 0;
    page_info[inited_pages].partitioned_node.page = &(page_info[inited_pages]);
    inited_pages++;
  }
}

void* kalloc_page() {
  _increment_rc(&alloc_page_cnt);
  // fetch_from_queue is atomic so we don't need to acquire a lock
  void* page_to_allocate = fetch_from_queue(&pages_free);
  return page_to_allocate;
}

void kfree_page(void* p) {
  _decrement_rc(&alloc_page_cnt);
  add_to_queue(&pages_free, (QueueNode*)p);
}

void* kalloc(isize s) {
  ASSERT(s > 0 && s <= PAGE_SIZE);
  if (s == 0 && s > PAGE_SIZE) return NULL;
  if (s > 2048) return kalloc_page();

  // Round the requested size to 2^n
  u32 rounded_size = 0;
  u8 bucket_index = 0;
  _round_up(s, &rounded_size, &bucket_index);

  setup_checker(0);
  acquire_spinlock(0, &pages_lock);

  if (partitioned_pages[bucket_index] == NULL) {
    PartitionedPageNode* new_partitioned =
        _partition_page(rounded_size, bucket_index);
    new_partitioned->head = 1;
    partitioned_pages[bucket_index] = (ListNode*)(new_partitioned);
    auto allocated = (void*)_alloc_partition(new_partitioned->page);
    release_spinlock(0, &pages_lock);
    return allocated;
  }

  if (partitioned_pages[bucket_index] != NULL) {
    ListNode* p_page = partitioned_pages[bucket_index];
    ListNode* head = p_page;

    // Find a partitioned page with free partitions
    while (((PartitionedPageNode*)p_page)->page->alloc_partitions_cnt + 1 >=
           (u32)((PAGE_SIZE /
                  (((PartitionedPageNode*)p_page)->page->base_size)))) {
      p_page = p_page->next;
      if (p_page == head) {
        PartitionedPageNode* new_partitioned =
            _partition_page(rounded_size, bucket_index);
        _merge_list(head, (ListNode*)new_partitioned);
        auto allocated = (void*)_alloc_partition(new_partitioned->page);
        release_spinlock(0, &pages_lock);
        return allocated;
      }
    }
    auto allocated =
        (void*)_alloc_partition(((PartitionedPageNode*)p_page)->page);
    release_spinlock(0, &pages_lock);
    return allocated;
  }

  release_spinlock(0, &pages_lock);
  return NULL;
}

void kfree(void* p) {
  u64 id = (u64)((((u64)p) - PAGE_BASE((u64)&end) - PAGE_SIZE) / PAGE_SIZE);

  setup_checker(0);
  acquire_spinlock(0, &pages_lock);

  (void)((page_info[id].alloc_partitions_cnt > 0) &&
         (page_info[id].alloc_partitions_cnt--));

  // Insert the free partition to be freed into the free list
  u64 tmp = *(u64*)(page_info[id].free_head);
  *(u64*)(page_info[id].free_head) = (u64)p;
  *(u64*)p = tmp;

  if (page_info[id].alloc_partitions_cnt == 0) {
    page_info[id].free_head = 0;
    page_info[id].base_size = 0;

    // Subtle. If we don't do so, there will be a dangling pointer
    if (page_info[id].partitioned_node.head) {
      if (partitioned_pages[page_info[id].partitioned_node.bucket_index] ==
          page_info[id].partitioned_node.next) {
        partitioned_pages[page_info[id].partitioned_node.bucket_index] = NULL;
      } else {
        partitioned_pages[page_info[id].partitioned_node.bucket_index] =
            page_info[id].partitioned_node.next;
      }
      ((PartitionedPageNode*)(page_info[id].partitioned_node.next))->head = 1;
    }
    _detach_from_list((ListNode*)(&(page_info[id].partitioned_node)));
    kfree_page((void*)(page_info[id].partitioned_node.page->addr));
  }
  release_spinlock(0, &pages_lock);
  return;
}

u16 _round_up(isize s, u32* rounded_size, u8* bucket_index) {
  if (s == 0) return 0;
  u16 result = 1;
  u8 index = 0;
  while (result < s) {
    result <<= 1;
    index += 1;
  }
  *rounded_size = result >= 8 ? result : 8;
  *bucket_index = index >= 3 ? index : 3;
  return result;
}

u64 _vaddr_to_id(u64 vaddr) {
  return (vaddr - PAGE_BASE((u64)&end) - PAGE_SIZE) / PAGE_SIZE;
}

PartitionedPageNode* _partition_page(u32 rounded_size, u8 bucket_index) {
  // fetch a new page which will be partitioned later
  u64 page_to_partition = (u64)kalloc_page();
  if (page_to_partition == NULL) return NULL;

  u64 p = page_to_partition;
  for (; p < page_to_partition + PAGE_SIZE - rounded_size; p += rounded_size) {
    *((u64*)p) = p + rounded_size;
  }
  *((u64*)p) = 0;

  // configure the control info of the page
  u64 id = _vaddr_to_id(page_to_partition);
  page_info[id].addr = page_to_partition;
  page_info[id].base_size = rounded_size;
  page_info[id].free_head = page_info[id].addr;
  page_info[id].partitioned_node.head = 0;
  page_info[id].partitioned_node.bucket_index = bucket_index;
  init_list_node((ListNode*)(&(page_info[id].partitioned_node)));
  return &(page_info[id].partitioned_node);
}

u64 _alloc_partition(Page* p) {
  u64 addr_frag = p->free_head;
  p->free_head = *(u64*)p->free_head;
  p->alloc_partitions_cnt++;
  return addr_frag;
}