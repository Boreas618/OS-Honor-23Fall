#include <aarch64/intrinsic.h>
#include <lib/string.h>
#include <kernel/mem.h>
#include <vm/pt.h>
#include <vm/paging.h>
#include <lib/printk.h>

PTEntry *
get_pte(struct vmspace *vms, u64 va, bool alloc) 
{
    if (!vms->pgtbl && !alloc)
        return NULL;

    if (!vms->pgtbl && alloc)
        vms->pgtbl = (PTEntry *)K2P(kalloc_page());

    /* We store the physical address of the page table in PCB. */
    PTEntry *pgtbl = (PTEntry *)P2K(vms->pgtbl);

    /* The 4 parts of the virtual address. */
    int idxs[] = {VA_PART0(va), VA_PART1(va), VA_PART2(va), VA_PART3(va)};

    /* The flags with respect to the four levels in page table. */
    u32 flags[] = {PTE_TABLE, PTE_TABLE, PTE_TABLE, PTE_USER_DATA};

    int i = 0;

    while (i < 3) {
        /* The PTE is invalid. */
        if (!(pgtbl[idxs[i]] & PTE_VALID)) {
            if (!alloc) return NULL;
            if (alloc) {
                pgtbl[idxs[i]] = K2P(kalloc_page()) | flags[i];
                memset((void *)P2K(PTE_ADDRESS(pgtbl[idxs[i]])), 0, PAGE_SIZE);
            }
        }
        pgtbl = (PTEntry *)P2K(PTE_ADDRESS(pgtbl[idxs[i]]));
        i++;
    }

    return (PTEntry *)(pgtbl + idxs[3]);
}

void 
init_vmspace(struct vmspace *vms) 
{ 
    vms->pgtbl = (PTEntry*)K2P(kalloc_page()); 
    memset((void*)P2K(vms->pgtbl), 0, PAGE_SIZE);
    init_spinlock(&vms->lock);
    init_vmregions(&vms->vmregions);
}

void 
free_pgdir(struct vmspace *vms) 
{
    if (vms->pgtbl == NULL)
        return;

    PTEntry *p_pgtbl_0 = (PTEntry *)P2K(vms->pgtbl);

    /* Recursively free the pages. */
    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        PTEntry *p_pte_level_0 = p_pgtbl_0 + i;
        if (!(*p_pte_level_0 & PTE_VALID))
            continue;

        for (int j = 0; j < N_PTE_PER_TABLE; j++) {
            PTEntry *p_pte_level_1 =
                (PTEntry *)P2K(PTE_ADDRESS(*p_pte_level_0)) + j;
            if (!(*p_pte_level_1 & PTE_VALID))
                continue;

            for (int k = 0; k < N_PTE_PER_TABLE; k++) {
                PTEntry *p_pte_level_2 =
                    (PTEntry *)P2K(PTE_ADDRESS(*p_pte_level_1)) + k;
                if (!(*p_pte_level_2 & PTE_VALID))
                    continue;

                kfree_page((void *)P2K(PTE_ADDRESS(*p_pte_level_2)));
            }
            kfree_page((void *)P2K(PTE_ADDRESS(*p_pte_level_1)));
        }
        kfree_page((void *)P2K(PTE_ADDRESS(*p_pte_level_0)));
    }
    kfree_page((void *)P2K(vms->pgtbl));
    vms->pgtbl = NULL;
}

void attach_vmspace(struct vmspace *vms) {
    extern PTEntries invalid_pt;
    if (vms->pgtbl)
        arch_set_ttbr0(K2P(vms->pgtbl));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void vmmap(struct vmspace *vs, u64 va, void *ka, u64 flags) {
    PTEntry* pte = get_pte(vs, va, true);
    *pte = (PTEntry) (K2P(ka) | flags);
    arch_tlbi_vmalle1is();
}