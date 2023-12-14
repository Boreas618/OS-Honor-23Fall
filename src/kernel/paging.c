#include <aarch64/mmu.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <lib/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <proc/proc.h>
#include <kernel/pt.h>
#include <proc/sched.h>

#define HEAP_BEGIN 0

define_rest_init(paging) {
    // TODO
}

void init_sections(List* vmregions) {
    list_init(vmregions);

    /* Init heap section. */
    struct vmregion* heap = (struct vmregion*)kalloc(sizeof(struct vmregion));
    heap->flags |= ST_HEAP;
    heap->begin = HEAP_BEGIN;
    heap->end = heap->begin + PAGE_SIZE;
    PTEntry *pte = get_pte(container_of(vmregions, struct vmspace, vmregions), HEAP_BEGIN, true);
    if (!pte) {
        printk("[Error] Failed to init heap section.\n");
        PANIC();
    }
    init_list_node(&heap->stnode);
    list_push_back(vmregions, &heap->stnode);
}

void free_sections(struct vmspace *pd) {
    (void)pd;
    // TODO
}

u64 sbrk(i64 size) {
    (void)size;
    // TODO:
    // Increase the heap size of current process by `size`
    // If `size` is negative, decrease heap size
    // `size` must be a multiple of PAGE_SIZE
    // Return the previous heap_end
    return 0;
}

int pgfault_handler(u64 iss) {
    struct proc *p = thisproc();
    struct vmspace *vs = &p->vmspace;
    u64 addr = arch_get_far(); // Attempting to access this address caused the
                               // page fault
    // TODO:
    // 1. Find the section struct that contains the faulting address `addr`
    // 2. Check section flags to determine page fault type
    // 3. Handle the page fault accordingly
    // 4. Return to user code or kill the process
    (void)iss;
    (void)vs;
    (void)addr;
    return 0;
}