#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/printk.h>
#include <lib/sem.h>
#include <lib/string.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <vm/paging.h>
#include <vm/pt.h>

#define HEAP_BEGIN 0

static void __free_range(u64 begin, u64 end) {
    for (; begin < end; begin += PAGE_SIZE) {
        // Get the PTE of the corresponding virtual address.
        pgtbl_entry_t *pte = get_pte(&thisproc()->vmspace, begin, false);
        if (!pte)
            continue;

        // Free the page that the pte points to and set the pte as invalid.
        kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
        *pte &= !PTE_VALID;
    }
}

void init_vmregions(List *vmregions) {
    list_init(vmregions);
    list_lock(vmregions);
    /* Init heap section. */
    struct vmregion *heap = (struct vmregion *)kalloc(sizeof(struct vmregion));
    heap->flags |= ST_HEAP;
    heap->begin = HEAP_BEGIN;
    heap->end = HEAP_BEGIN;
    init_list_node(&heap->stnode);
    list_push_back(vmregions, &heap->stnode);
    list_unlock(vmregions);
}

void free_vmregions(struct vmspace *vms) {
    struct list vmregions = vms->vmregions;
    list_lock(&vmregions);
    while (vmregions.size) {
        struct vmregion *vmr =
            container_of(vmregions.head, struct vmregion, stnode);
        __free_range(vmr->begin, vmr->end);
        list_remove(&vmregions, &vmr->stnode);
        kfree(vmr);
    }
    list_unlock(&vmregions);
}

u64 sbrk(i64 size) {
    ASSERT(size % PAGE_SIZE == 0);
    u64 old_end = 0;
    u64 new_end = 0;
    list_forall(p, thisproc()->vmspace.vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if (v->flags & ST_HEAP) {
            // Save the old end address of the vmregion.
            old_end = v->end;

            // Set the new end address of the vmregion.
            v->end += size;
            new_end = v->end;

            // If the heap size shrinks, we need to first check if the new end
            // is above the address where the heap section begins. Then we free
            // the pages that are not used by heap after the change of heap
            // size.
            if (new_end < old_end) {
                if (new_end < v->begin) {
                    printk("[Error] Invalid size.\n");
                    return -1;
                }
                __free_range(new_end, old_end);
            }

            goto heap_found;
        }
    }

    // Heap section not found.
    printk("[Error] Heap section not found.\n");
    return -1;

heap_found:
    list_forall(p, thisproc()->vmspace.vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if ((v->begin < new_end) && (new_end < v->end)) {
            printk("[Error] Invalid size.\n");
            return -1;
        }
    }
    return old_end;
}

int pgfault_handler(u64 iss) {
    struct proc *p = thisproc();
    struct vmspace *vs = &p->vmspace;
    /* The address which caused the page fault. */
    u64 addr = arch_get_far();
    (void)iss;

    list_forall(p, vs->vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if (v->begin <= addr && addr < v->end) {
            if (v->flags & ST_HEAP) {
                vmmap(vs, addr, kalloc_page(), PTE_VALID | PTE_USER_DATA);
            } else {
                printk("[Error] Invalid Memory Access.");
                if (kill(thisproc()->pid) == -1)
                    printk("[Error] Failed to kill.");
                PANIC();
            }
        }
    }

    // We have modified the page table. Therefore, we should flush TLB to ensure
    // consistency.
    arch_tlbi_vmalle1is();
    return 0;
}