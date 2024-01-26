#include <aarch64/intrinsic.h>
#include <aarch64/mmu.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <lib/checker.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/printk.h>
#include <lib/rc.h>
#include <lib/spinlock.h>
#include <lib/string.h>
#include <kernel/param.h>

RefCount alloc_page_cnt;

/*
 * Memory is divided into pages. During the initialization process of the
 * kernel, all the available pages are stored in the pages_free variable. Memory
 * can be allocated in two ways: as a whole page or as a tiny partition. Pages
 * have a fixed size of 4096 bytes, while partitions can range in size from 8 to
 * 2048 bytes.
 *
 * Why 8 to 2048? Because we need to maintain a linked list of free partitions
 * and the pointer to the next free partition is stored in the former free
 * partition. The partition should be at least 8 bytes in order to store a
 * pointer. We choose 2048 as the maximum size of a partition because a
 * partition with 4096 bytes is a whole page.
 */

/* The array for information about pages. */
static struct page page_info[MAX_PAGES];

/* The queue for all pages available. It is placed in BSS segment. */
static struct list pages_free;

/* The hash map for pages with different maximum sizes of available area. */
static struct list slabs[SLAB_MAX_ORDER + 1];

/*
 * The handle for accessing memory area that is ready for allocating.
 * We can access to the area by referencing this handle.
 * See linker.ld.
 */
extern char end[];

/* The shared zero page. */
void *zero;

/* Initialization routine of the memory management module. */
define_early_init(pages)
{
	/* Initialize the page counter. */
	init_rc(&alloc_page_cnt);
	/* Initialize the list for free pages. */
	list_init(&pages_free);
	/* The number of pages under track. */
	u64 inited_pages = 0;

	for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP);
	     p += PAGE_SIZE) {
		list_push_back(&pages_free, (ListNode *)p);

		if (inited_pages >= MAX_PAGES)
			PANIC();

		page_info[inited_pages].addr = p;
		page_info[inited_pages].base_size = 0;
		page_info[inited_pages].free_head =
			page_info[inited_pages].addr;
		page_info[inited_pages].alloc_partitions_cnt = 0;
		page_info[inited_pages].partitioned_node.page =
			&(page_info[inited_pages]);

		inited_pages++;
	}

	/* Initialize the slab lists */
	for (int i = 0; i < SLAB_MAX_ORDER + 1; i++)
		list_init(&slabs[i]);
}

define_init(zero_page)
{
	zero = (void *)kalloc_page();
	memset(zero, 0, PAGE_SIZE);
}

void *kalloc_page()
{
	increment_rc(&alloc_page_cnt);
	list_lock(&pages_free);
	void *page_to_allocate = (void *)pages_free.head;
	list_pop_head(&pages_free);
	list_unlock(&pages_free);
	return page_to_allocate;
}

void kfree_page(void *p)
{
	decrement_rc(&alloc_page_cnt);
	list_lock(&pages_free);
	list_push_back(&pages_free, (ListNode *)p);
	list_unlock(&pages_free);
}

void *kalloc(isize s)
{
	ASSERT(s > 0 && s <= PAGE_SIZE);
	if (s == 0 && s > PAGE_SIZE)
		return NULL;
	if (s > 2048)
		return kalloc_page();

	/* Round the requested size to 2^n. */
	u32 rounded_size = 0;
	u8 bucket_index = 0;
	__round_up(s, &rounded_size, &bucket_index);

	/* Acquire the bucket-level lock. */
	list_lock(&(slabs[bucket_index]));

	/* In most cases, there are partitioned pages in the bucket and we can fetch
     * partitions from them. */
	if (slabs[bucket_index].size) {
		/* Take the list of candidate pages from the bucket. */
		List candidate_pages = slabs[bucket_index];

		/* Find the page with a free partition in the candidate pages. */
		usize i = 0;
		for (ListNode *p = candidate_pages.head;
		     i < candidate_pages.size; p = p->next, i++) {
			Page *page =
				container_of(p, PartitionedNode, pp_node)->page;
			if (page->alloc_partitions_cnt + 1 <
			    (u32)(PAGE_SIZE / (page->base_size))) {
				void *allocated =
					(void *)__alloc_partition(page);
				list_unlock(&(slabs[bucket_index]));
				return allocated;
			}
		}
	}

	/* Cannot find the page with a free partition. Therefore, fetch a new page
     * and partition it. */
	PartitionedNode *new_partitioned =
		__partition_page(rounded_size, bucket_index);
	list_push_back(&(slabs[bucket_index]), &(new_partitioned->pp_node));
	void *allocated = (void *)__alloc_partition(new_partitioned->page);
	list_unlock(&(slabs[bucket_index]));
	return allocated;
}

void kfree(void *p)
{
	u64 id = (u64)((((u64)p) - PAGE_BASE((u64)&end) - PAGE_SIZE) /
		       PAGE_SIZE);

	list_lock(&slabs[page_info[id].partitioned_node.bucket_index]);

	(void)((page_info[id].alloc_partitions_cnt > 0) &&
	       (page_info[id].alloc_partitions_cnt--));

	/* Insert the free partition to be freed into the free list */
	u64 tmp = *(u64 *)(page_info[id].free_head);
	*(u64 *)(page_info[id].free_head) = (u64)p;
	*(u64 *)p = tmp;

	/* Recycle the partitioned pages if the partitions are all freed. */
	if (page_info[id].alloc_partitions_cnt == 0) {
		list_remove(&slabs[page_info[id].partitioned_node.bucket_index], &page_info[id].partitioned_node.pp_node);
		kfree_page((void *)(page_info[id].partitioned_node.page->addr));
	}

	list_unlock(&slabs[page_info[id].partitioned_node.bucket_index]);
	return;
}

u16 __round_up(isize s, u32 *rounded_size, u8 *bucket_index)
{
	if (s == 0)
		return 0;
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

PartitionedNode *__partition_page(u32 rounded_size, u8 bucket_index)
{
	/* Fetch a new page which will be partitioned later. */
	u64 page_to_partition = (u64)kalloc_page();
	if (page_to_partition == NULL)
		return NULL;

	u64 p = page_to_partition;
	for (; p < page_to_partition + PAGE_SIZE - rounded_size;
	     p += rounded_size)
		*((u64 *)p) = p + rounded_size;
	*((u64 *)p) = 0;

	/* Configure the control info of the page. */
	u64 id = VA2ID(page_to_partition);
	page_info[id].addr = page_to_partition;
	page_info[id].base_size = rounded_size;
	page_info[id].free_head = page_info[id].addr;
	page_info[id].partitioned_node.bucket_index = bucket_index;
	init_list_node((ListNode *)(&(page_info[id].partitioned_node)));
	return &(page_info[id].partitioned_node);
}

u64 __alloc_partition(Page *p)
{
	u64 addr_frag = p->free_head;
	p->free_head = *(u64 *)p->free_head;
	p->alloc_partitions_cnt++;
	return addr_frag;
}

u64 left_page_cnt()
{
	return PAGE_COUNT - alloc_page_cnt.count;
}

WARN_RESULT void *get_zero_page()
{
	if (!zero) {
		printk("[Error] Zero page not initialized!");
		return NULL;
	}
	return zero;
}

struct page *get_page_info_by_kaddr(void *kaddr)
{
	isize id = VA2ID((u64)kaddr);
	if (id < 0 || id >= MAX_PAGES) {
		return NULL;
	}
	return &page_info[id];
}