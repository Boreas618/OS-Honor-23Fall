#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <common/defines.h>
#define OBJ_SIZE 16
RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}

extern char end[];
static QueueNode* pages;
static QueueNode* slab[PAGE_SIZE / OBJ_SIZE];
define_early_init(init_page) {
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p); 
    }
}

void* kalloc_page() {
    _increment_rc(&alloc_page_cnt);
    // TODO
    return fetch_from_queue(&pages);
}

void kfree_page(void* p) {
    _decrement_rc(&alloc_page_cnt);
    // TODO
    add_to_queue(&pages, (QueueNode*)p);
}

// TODO: kalloc kfree
void* kalloc(isize size) {
    size = (size + OBJ_SIZE - 1) / OBJ_SIZE;
    void* ret = NULL;
    while ((ret =  fetch_from_queue(&slab[size])) == NULL) {
        void *p = kalloc_page();
        *(u64*) p = size;
        p += 8;
        for (int i = 0; i < (PAGE_SIZE - 8) / (OBJ_SIZE * size); i++) {
            add_to_queue(&slab[size], p);
            p += size * OBJ_SIZE;
        }
    }
    return ret;
}

void kfree(void* p) {
    void *page_start = (void*)((u64)p & ~(PAGE_SIZE - 1));
    u64 size = *(u64*)page_start;
    add_to_queue(&slab[size], p);
}