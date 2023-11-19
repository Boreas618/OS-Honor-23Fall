#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/printk.h>

PTEntry* get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    if (!pgdir->pt && !alloc) 
        return NULL;
    if (!pgdir->pt && alloc)
        pgdir->pt = (PTEntry*) K2P(kalloc_page());

    PTEntry* pgtbl = (PTEntry*) P2K(pgdir->pt);
    int idxs[] = { VA_PART0(va), VA_PART1(va), VA_PART2(va), VA_PART3(va) };
    unsigned int flags[] = { PTE_TABLE, PTE_TABLE, PTE_TABLE, PTE_USER_DATA };

    for (int i = 0; i < 4; i++) {
        if (!(pgtbl[idxs[i]] & PTE_VALID)) {
            if (!alloc) return NULL;
            (void)((alloc) &&(i != 3) && (pgtbl[idxs[i]] = K2P(kalloc_page()) | flags[i]) && (memset((void*)P2K(PTE_ADDRESS(pgtbl[idxs[i]])), 0, PAGE_SIZE)));
        }
        (void)((i != 3) && (pgtbl = (PTEntry*) P2K(PTE_ADDRESS(pgtbl[idxs[i]]))));
    }

    return (PTEntry*)(pgtbl + idxs[3]);
}


void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir* pgdir)
{
    if (pgdir->pt == NULL)
        return;

    PTEntry* p_pgtbl_0 = (PTEntry*) P2K(pgdir->pt);

    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        PTEntry* p_pte_level_0 = p_pgtbl_0+i;

        if (!(*p_pte_level_0 & PTE_VALID))
            continue;

        for (int j = 0; j < N_PTE_PER_TABLE; j++) {
            PTEntry* p_pte_level_1 = (PTEntry*) P2K(PTE_ADDRESS(*p_pte_level_0)) + j;

            if (!(*p_pte_level_1 & PTE_VALID))
                continue;

            for (int k = 0; k < N_PTE_PER_TABLE; k++) {
                PTEntry* p_pte_level_2 = (PTEntry*) P2K(PTE_ADDRESS(*p_pte_level_1)) + k;

                if (!(*p_pte_level_2 & PTE_VALID))
                    continue;

                for (int l = 0; l < N_PTE_PER_TABLE; l++) {
                    PTEntry* p_pte_level_3 = (PTEntry*) P2K(PTE_ADDRESS(*p_pte_level_2)) + l;

                    if (!(*p_pte_level_3 & PTE_VALID))
                        continue;

                    //kfree_page((void*) P2K(PTE_ADDRESS(*p_pte_level_3)));
                 }
                kfree_page((void*) P2K(PTE_ADDRESS(*p_pte_level_2)));
            }
            kfree_page((void*) P2K(PTE_ADDRESS(*p_pte_level_1)));
        }
        kfree_page((void*) P2K(PTE_ADDRESS(*p_pte_level_0)));
    }

    kfree_page((void*) P2K(pgdir->pt));

    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}