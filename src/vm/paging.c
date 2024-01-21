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

void copy_vmspace(struct vmspace *vms_source, struct vmspace *vms_dest) {
    // kfree_page((void*)(vms_dest->pgtbl));

    // Clear the vmregions of the dest vmspace.
    free_vmregions(vms_dest);

    // Copy the page table.
    vms_dest->pgtbl = vms_source->pgtbl;

    list_forall(p, vms_source->vmregions) {
        struct vmregion *vmr = container_of(p, struct vmregion, stnode);
        printk("Start: %llx | End: %llx %llx\n", (u64)vmr->begin, (u64)vmr->end,
               (u64)vmr->flags);
    }

    // Copy the vmregions to the dest vmspace.
    list_forall(p, vms_source->vmregions) {
        struct vmregion *vmr = container_of(p, struct vmregion, stnode);
        create_vmregion(vms_dest, vmr->flags, vmr->begin,
                        vmr->end - vmr->begin);
    }
}

void init_vmspace(struct vmspace *vms) {
    vms->pgtbl = (pgtbl_entry_t *)K2P(kalloc_page());
    memset((void *)P2K(vms->pgtbl), 0, PAGE_SIZE);
    init_spinlock(&vms->lock);
    list_init(&vms->vmregions);
}

void destroy_vmspace(struct vmspace *vms) {
    free_vmregions(vms);
    free_page_table(&(vms->pgtbl));
    kfree((void *)vms);
}

bool check_vmregion_intersection(struct vmspace *vms, u64 begin, u64 end) {
    list_forall(p, vms->vmregions) {
        struct vmregion *vmr = container_of(p, struct vmregion, stnode);
        if ((begin >= vmr->begin && begin < vmr->end) ||
            (end >= vmr->begin && end < vmr->end))
            return true;
    }
    return false;
}

struct vmregion *create_vmregion(struct vmspace *vms, u64 flags, u64 begin,
                                 u64 len) {
    if (check_vmregion_intersection(vms, begin, begin + len))
        return NULL;

    struct vmregion *vmr = (struct vmregion *)kalloc(sizeof(struct vmregion));
    memset(vmr, 0, sizeof(struct vmregion));
    init_list_node(&vmr->stnode);
    vmr->flags = flags;
    vmr->begin = begin;
    vmr->end = begin + len;
    list_push_back(&vms->vmregions, &vmr->stnode);
    return vmr;
}

void free_vmregions(struct vmspace *vms) {
    list_lock(&vms->vmregions);
    while (vms->vmregions.size) {
        struct vmregion *vmr =
            container_of(vms->vmregions.head, struct vmregion, stnode);
        printk("Free: Start: %llx | End: %llx\n", (u64)vmr->begin,
               (u64)vmr->end);
        unmap_range_in_pgtbl(vms->pgtbl, vmr->begin, vmr->end);
        list_remove(&vms->vmregions, &vmr->stnode);
        kfree(vmr);
    }
    list_unlock(&vms->vmregions);
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
                unmap_range_in_pgtbl(thisproc()->vmspace.pgtbl, new_end,
                                     old_end);
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

    ASSERT(addr);

    // printk("%p\n", (void*)PTE_ADDRESS(*get_pte(p->vmspace.pgtbl, addr,
    // false)));

    (void)iss;

    printk("%llx\n", (u64)addr);

    ASSERT(1 == 2);

    list_forall(p, vs->vmregions) {
        struct vmregion *v = container_of(p, struct vmregion, stnode);
        if (v->begin <= addr && addr < v->end) {
            if (v->flags & ST_HEAP) {
                map_in_pgtbl(vs->pgtbl, addr, kalloc_page(),
                             PTE_VALID | PTE_USER_DATA);
            } else {
                printk("%p\n",
                       (void *)(P2K(PTE_ADDRESS(*get_pte(
                           thisproc()->vmspace.pgtbl, 0x40014c, false)))));
                printk("%p\n",
                       (void *)(P2K(PTE_ADDRESS(*get_pte(
                           thisproc()->vmspace.pgtbl, 0x402198, false)))));
                printk("An: %p\n",
                       (void *)(P2K(PTE_ADDRESS(*get_pte(
                           thisproc()->vmspace.pgtbl, 0x404000, false)))));
                // auto pte = get_pte(thisproc()->vmspace.pgtbl, addr, false);
                // printk("???: %p\n", (void*)P2K(PTE_ADDRESS(*pte)));
                printk("[Error] Unsupported page fault.");
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