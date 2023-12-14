#include <aarch64/mmu.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <lib/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <vm/paging.h>
#include <lib/printk.h>
#include <proc/proc.h>
#include <vm/pt.h>
#include <proc/sched.h>

#define HEAP_BEGIN 0

define_rest_init(paging) {
    // TODO
}

void 
init_vmregions(List* vmregions)
{
    list_init(vmregions);
    list_lock(vmregions);
    /* Init heap section. */
    struct vmregion* heap = (struct vmregion*)kalloc(sizeof(struct vmregion));
    heap->flags |= ST_HEAP;
    heap->begin = HEAP_BEGIN;
    heap->end = HEAP_BEGIN;
    init_list_node(&heap->stnode);
    list_push_back(vmregions, &heap->stnode);
    list_unlock(vmregions);
}

void 
free_vmregions(struct vmspace *vms) 
{
    List vmregions = vms->vmregions;
    list_lock(&vmregions);
    while (vmregions.size) {
        struct vmregion* vmr = container_of(vmregions.head, struct vmregion, stnode);
        u64 b = vmr->begin;
        u64 e = vmr->end;
        while (b < e) {
            PTEntry* pte = get_pte(&thisproc()->vmspace, b, false);
            b += PAGE_SIZE;
            if (!pte) continue;
            *pte &= !PTE_VALID;
        }
        list_remove(&vmregions, &vmr->stnode);
        kfree(vmr);
    }
    list_unlock(&vmregions);
}

u64 
sbrk(i64 size) 
{
    ASSERT(size % PAGE_SIZE == 0);
    u64 old = 0;
    u64 new = 0;
    list_forall(p, thisproc()->vmspace.vmregions) {
        struct vmregion* v = container_of(p, struct vmregion, stnode);
        if (v->flags & ST_HEAP) {
            old = v->end; 
            v->end += size;
            new = v->end;
            while (new < old) {
                PTEntry* pte = get_pte(&thisproc()->vmspace, new, false);
                new += PAGE_SIZE;
                if (!pte) continue;
                *pte &= !PTE_VALID;
            }
            goto heap_found;
        }
    }
    
    /* Heap section not found. */
    printk("[Error] Heap section not found.\n");
    return -1;

    /* The new heap region is invalid. */
    heap_found:
    list_forall(p, thisproc()->vmspace.vmregions) {
        struct vmregion* v = container_of(p, struct vmregion, stnode);
        if (v->begin < new && new < v->end) {
            printk("[Error] Invalid size.\n");
            return -1; 
        }
    }
    return old;
}

int 
pgfault_handler(u64 iss) 
{
    struct proc *p = thisproc();
    struct vmspace *vs = &p->vmspace;
    /* The address which caused the page fault. */
    u64 addr = arch_get_far();
    (void)iss;
    
    list_forall(p, vs->vmregions) {
        struct vmregion* v = container_of(p, struct vmregion, stnode);
        if (v->begin <= addr && addr < v->end) {
            if (v->flags & ST_HEAP) {
                vmmap(vs, addr, kalloc_page(), PTE_VALID | PTE_USER_DATA);
                if (!p) {
                    printk("[Error] Lazy allocation failed.\n");
                    PANIC();
                }
            } else {
                printk("[Error] Invalid Memory Access.");
                if (kill(thisproc()->pid) == -1) {
                    printk("[Error] Failed to kill.");
                }
                PANIC();
            }
        }
    }
    arch_tlbi_vmalle1is();
    return 0;
}