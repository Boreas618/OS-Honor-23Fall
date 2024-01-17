#include <aarch64/intrinsic.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <vm/paging.h>
#include <vm/pt.h>

pgtbl_entry_t *get_pte(struct vmspace *vms, u64 va, bool alloc) {
    if (!vms->pgtbl && !alloc)
        return NULL;

    if (!vms->pgtbl && alloc)
        vms->pgtbl = (pgtbl_entry_t *)K2P(kalloc_page());

    /* We store the physical address of the page table in PCB. */
    pgtbl_entry_t *pgtbl = (pgtbl_entry_t *)P2K(vms->pgtbl);

    /* The 4 parts of the virtual address. */
    int idxs[] = {VA_PART0(va), VA_PART1(va), VA_PART2(va), VA_PART3(va)};

    /* The flags with respect to the four levels in page table. */
    u32 flags[] = {PTE_TABLE, PTE_TABLE, PTE_TABLE, PTE_USER_DATA};

    int i = 0;

    while (i < 3) {
        /* The PTE is invalid. */
        if (!(pgtbl[idxs[i]] & PTE_VALID)) {
            if (!alloc)
                return NULL;
            if (alloc) {
                pgtbl[idxs[i]] = K2P(kalloc_page()) | flags[i];
                memset((void *)P2K(PTE_ADDRESS(pgtbl[idxs[i]])), 0, PAGE_SIZE);
            }
        }
        pgtbl = (pgtbl_entry_t *)P2K(PTE_ADDRESS(pgtbl[idxs[i]]));
        i++;
    }

    return (pgtbl_entry_t *)(pgtbl + idxs[3]);
}

void init_vmspace(struct vmspace *vms) {
    vms->pgtbl = (pgtbl_entry_t *)K2P(kalloc_page());
    memset((void *)P2K(vms->pgtbl), 0, PAGE_SIZE);
    init_spinlock(&vms->lock);
    init_vmregions(&vms->vmregions);
}

void free_vmspace(struct vmspace *vms) {
    if (vms->pgtbl == NULL)
        return;

    pgtbl_entry_t *p_pgtbl_0 = (pgtbl_entry_t *)P2K(vms->pgtbl);

    /* Recursively free the pages. */
    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        pgtbl_entry_t *p_pte_level_0 = p_pgtbl_0 + i;
        if (!(*p_pte_level_0 & PTE_VALID))
            continue;

        for (int j = 0; j < N_PTE_PER_TABLE; j++) {
            pgtbl_entry_t *p_pte_level_1 =
                (pgtbl_entry_t *)P2K(PTE_ADDRESS(*p_pte_level_0)) + j;
            if (!(*p_pte_level_1 & PTE_VALID))
                continue;

            for (int k = 0; k < N_PTE_PER_TABLE; k++) {
                pgtbl_entry_t *p_pte_level_2 =
                    (pgtbl_entry_t *)P2K(PTE_ADDRESS(*p_pte_level_1)) + k;
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
    pgtbl_entry_t *pte = get_pte(vs, va, true);
    *pte = (pgtbl_entry_t)(K2P(ka) | flags);
    arch_tlbi_vmalle1is();
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct vmspace *pd, void *va, void *p, usize len) {
    // TODO
    (void)pd;
    (void)va;
    (void)p;
    (void)len;
    return 0;
}