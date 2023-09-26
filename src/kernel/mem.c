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

u64 _alloc_fragment(Page* p);

RefCount alloc_page_cnt;

RefCount free_cnt;

FragNode* _fragment_page(u32 rounded_size, u8 bucket_index);

/*
   memory is divided into pages. During the initialization process of the
   kernel, all the available pages are stored in the pages_free variable. Memory
   can be allocated in two ways: as a whole page or as a fragment. Pages have a
   fixed size of 4096 bytes, while fragments can range in size from 0 to 4096
   bytes. A page can be allocated either entirely or in fragments.
*/

/* The array for information about pages. */
static Page page_info[MAX_PAGES];

/* The queue for all pages available. It is placed in BSS segment.*/
static QueueNode* pages_free;

/* The hash map for pages with different maximum sizes of available area.*/
static ListNode* fragments_free[MAX_BUCKETS];

static SpinLock* pages_lock;

static SpinLock* fragments_lock;

/* The handle for accessing memory area that is ready for allocating.
   We can access to the area by referencing this handle.
   See linker.ld
*/
extern char end[];

/* Initialization routine which initialize alloc_page_cnt to 0 */
define_early_init(alloc_page_cnt) { init_rc(&alloc_page_cnt); }

/* Initialization routine which starts tracking all of the pages */
define_early_init(pages) {
  init_spinlock(pages_lock);
  init_spinlock(fragments_lock);
  u64 inited_pages = 0;

  for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP);
       p += PAGE_SIZE) {
    add_to_queue(&pages_free, (QueueNode*)p);

    if (inited_pages >= MAX_PAGES) break;
    page_info[inited_pages].addr = p;
    page_info[inited_pages].base_size = 0;
    page_info[inited_pages].flag = 0;
    page_info[inited_pages].candidate_idx = 0;
    page_info[inited_pages].alloc_fragments_cnt = 0;
    page_info[inited_pages].frag_node.page = &(page_info[inited_pages]);
    inited_pages++;
  }
}

void* kalloc_page() {
  setup_checker(0);
  acquire_spinlock(0, pages_lock);
  _increment_rc(&alloc_page_cnt);
  _increment_rc(&free_cnt);
  void* page_to_allocate = fetch_from_queue(&pages_free);
  release_spinlock(0, pages_lock);
  return page_to_allocate;
}

void kfree_page(void* p) {
  setup_checker(1);
  acquire_spinlock(1, pages_lock);
  _decrement_rc(&alloc_page_cnt);
  memset(p, 0, (usize)PAGE_SIZE);
  add_to_queue(&pages_free, (QueueNode*)p);
  release_spinlock(1, pages_lock);
}

void* kalloc(isize s) {
  if (s == 0 && s > PAGE_SIZE) return NULL;
  if (s > 2048) return kalloc_page();

  // Round the requested size to 2^n
  u32 rounded_size = 0;
  u8 bucket_index = 0;
  _round_up(s, &rounded_size, &bucket_index);

  // Try to get the address
  if (fragments_free[bucket_index] == NULL) {
    FragNode* new_fraged = _fragment_page(rounded_size, bucket_index);
    new_fraged->head = 1;
    fragments_free[bucket_index] = (ListNode*)(new_fraged);
    return (void*)_alloc_fragment(new_fraged->page);
  } else {
    ListNode* p_page = fragments_free[bucket_index];
    ListNode* head = p_page;

    while (((FragNode*)p_page)->page->candidate_idx >=
           (u32)((PAGE_SIZE / (((FragNode*)p_page)->page->base_size)))) {
      p_page = p_page->next;
      if (p_page == head) {
        FragNode* new_fraged = _fragment_page(rounded_size, bucket_index);
        setup_checker(0);
        merge_list(0, fragments_lock, head, (ListNode*)new_fraged);
        return (void*)_alloc_fragment(new_fraged->page);
      }
    }
    return (void*)_alloc_fragment(((FragNode*)p_page)->page);
  }
  return NULL;
}

void kfree(void* p) {
  u64 id = (u64)((((u64)p) - PAGE_BASE((u64)&end) - PAGE_SIZE) / PAGE_SIZE);

  setup_checker(0);
  acquire_spinlock(0, pages_lock);
  (void)((page_info[id].alloc_fragments_cnt > 0) &&
         (page_info[id].alloc_fragments_cnt--));
  release_spinlock(0, pages_lock);

  if (page_info[id].alloc_fragments_cnt == 0) {
    page_info[id].candidate_idx = 0;
    page_info[id].flag = 0;
    page_info[id].base_size = 0;

    if (page_info[id].frag_node.head) {
      setup_checker(2);
      acquire_spinlock(2, fragments_lock);
      if (fragments_free[page_info[id].frag_node.bucket_index] ==
          page_info[id].frag_node.next) {
        fragments_free[page_info[id].frag_node.bucket_index] = NULL;
      } else {
        fragments_free[page_info[id].frag_node.bucket_index] =
            page_info[id].frag_node.next;
      }
      release_spinlock(2, fragments_lock);

      setup_checker(3);
      acquire_spinlock(3, pages_lock);
      ((FragNode*)(page_info[id].frag_node.next))->head = 1;
      release_spinlock(3, pages_lock);
    }

    setup_checker(1);
    detach_from_list(1, fragments_lock,
                     (ListNode*)(&(page_info[id].frag_node)));
    kfree_page((void*)(page_info[id].frag_node.page->addr));
  }
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
  *rounded_size = result;
  *bucket_index = index;
  return result;
}

u64 _vaddr_to_id(u64 vaddr) {
  return (vaddr - PAGE_BASE((u64)&end) - PAGE_SIZE) / PAGE_SIZE;
}

FragNode* _fragment_page(u32 rounded_size, u8 bucket_index) {
  // fetch a new page which will be fragmented later
  u64 page_to_fragment = (u64)kalloc_page();
  if (page_to_fragment == NULL) return NULL;

  // configure the control info of the page
  u64 id = _vaddr_to_id(page_to_fragment);
  page_info[id].addr = page_to_fragment;
  page_info[id].base_size = rounded_size;
  page_info[id].flag |= PAGE_FRAGMENTED;
  page_info[id].frag_node.head = 0;
  page_info[id].frag_node.bucket_index = bucket_index;
  init_list_node((ListNode*)(&(page_info[id].frag_node)));
  return &(page_info[id].frag_node);
}

u64 _alloc_fragment(Page* p) {
  setup_checker(0);
  acquire_spinlock(0, pages_lock);
  u64 addr_frag = p->addr + (p->candidate_idx) * (p->base_size);
  p->candidate_idx++;
  p->alloc_fragments_cnt++;
  release_spinlock(0, pages_lock);
  return addr_frag;
}