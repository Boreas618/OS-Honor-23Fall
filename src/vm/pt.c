#include <aarch64/intrinsic.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <vm/paging.h>
#include <vm/pt.h>
#include <proc/sched.h>

pgtbl_entry_t *get_pte(pgtbl_entry_t *pt, u64 va, bool alloc) {
    if (!pt && !alloc)
        return NULL;

    if (!pt && alloc)
        pt = (pgtbl_entry_t *)K2P(kalloc_page());

    /* We store the physical address of the page table in PCB. */
    pgtbl_entry_t *pgtbl = (pgtbl_entry_t *)P2K(pt);

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

void free_page_table(pgtbl_entry_t **pt) {
    if (*pt == NULL)
        return;

    pgtbl_entry_t *p_pgtbl_0 = (pgtbl_entry_t *)P2K(*pt);

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
    kfree_page((void *)P2K(*pt));
    *pt = NULL;
}

void set_page_table(pgtbl_entry_t* pt) {
    extern PTEntries invalid_pt;
    if (pt)
        arch_set_ttbr0(K2P(pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void map_range_in_pgtbl(pgtbl_entry_t *pt, u64 va, void *ka, u64 flags) {
    pgtbl_entry_t *pte = get_pte(pt, va, true);
    *pte = (pgtbl_entry_t)(K2P(ka) | flags);
    arch_tlbi_vmalle1is();
}

void unmap_range_in_pgtbl(pgtbl_entry_t *pt, u64 begin, u64 end) {
    ASSERT((end - begin) % PAGE_SIZE == 0);
    for (; begin < end; begin += PAGE_SIZE) {
        // Get the PTE of the corresponding virtual address.
        pgtbl_entry_t *pte = get_pte(pt, begin, false);
        if (!pte) continue;
        // Free the page that the pte points to and set the pte as invalid.
        kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
        *pte &= !PTE_VALID;
    }
}

void freeze_pgtbl(pgtbl_entry_t* pt) {
    pgtbl_entry_t *p_pgtbl_0 = (pgtbl_entry_t *)P2K(pt);

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
                if (!(*p_pte_level_2 & PTE_VALID) || *p_pte_level_2 & PTE_RO)
                    continue;

                *p_pte_level_2 = *p_pte_level_2 | PTE_RO;
            }
            *p_pte_level_1 = *p_pte_level_1 | PTE_RO;
        }
        *p_pte_level_0 = *p_pte_level_0 | PTE_RO;
    }
    *pt = *pt | PTE_RO;
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