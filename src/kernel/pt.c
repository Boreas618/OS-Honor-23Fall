#include <aarch64/intrinsic.h>
#include <lib/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>

PTEntry *
get_pte(struct vmspace *pgdir, u64 va, bool alloc) 
{
    if (!pgdir->pt && !alloc)
        return NULL;

    if (!pgdir->pt && alloc)
        pgdir->pt = (PTEntry *)K2P(kalloc_page());

    /* We store the physical address of the page table in PCB. */
    PTEntry *pgtbl = (PTEntry *)P2K(pgdir->pt);

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

void init_pgdir(struct vmspace *pgdir) { pgdir->pt = NULL; }

void 
free_pgdir(struct vmspace *pgdir) 
{
    if (pgdir->pt == NULL)
        return;

    PTEntry *p_pgtbl_0 = (PTEntry *)P2K(pgdir->pt);

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
    kfree_page((void *)P2K(pgdir->pt));
    pgdir->pt = NULL;
}

void attach_pgdir(struct vmspace *pgdir) {
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void vmmap(struct vmspace *pd, u64 va, void *ka, u64 flags) {
    // TODO
    // Map virtual address 'va' to the physical address represented by kernel
    // address 'ka' in page directory 'pd', 'flags' is the flags for the page
    // table entry
    (void)pd;
    (void)va;
    (void)ka;
    (void)flags;
}