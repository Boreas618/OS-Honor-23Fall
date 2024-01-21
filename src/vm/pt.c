#include <aarch64/intrinsic.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <proc/sched.h>
#include <vm/paging.h>
#include <vm/pt.h>

// #define PT_DEBUG

void detach_mapped_page(pgtbl_entry_t *pte) {
    u64 page_addr_k = (u64)P2K(PTE_ADDRESS(*pte));
    struct page *page_mapped = get_page_info_by_kaddr((void *)page_addr_k);
    if (page_mapped) {
        decrement_rc(&page_mapped->ref);
        if (page_mapped->ref.count == 0)
            kfree_page((void *)page_addr_k);
    }
}

pgtbl_entry_t *get_pte(pgtbl_entry_t *pt, u64 va, bool alloc) {
    if (!pt && !alloc)
        return NULL;

    if (!pt && alloc)
        pt = (pgtbl_entry_t *)K2P(kalloc_page());

    // We store the physical address of the page table in PCB.
    pgtbl_entry_t *pgtbl = (pgtbl_entry_t *)P2K(pt);

    // The 4 parts of the virtual address.
    int idxs[] = {VA_PART0(va), VA_PART1(va), VA_PART2(va), VA_PART3(va)};

    // The flags with respect to the four levels in page table.
    u32 flags[] = {PTE_TABLE, PTE_TABLE, PTE_TABLE, PTE_USER_DATA};

    int i = 0;

    while (i < 3) {
        // The PTE is invalid.
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

void __free_page_table_level(pgtbl_entry_t *p_pte_page, u8 level) {
    ASSERT(level <= 3);

    // Get the pointer to the first PTE in the current PTE page.
    pgtbl_entry_t *p_pte_base = (pgtbl_entry_t *)P2K(PTE_ADDRESS(*p_pte_page));

    // Iterate over each PTE in the table.
    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        pgtbl_entry_t *p_pte = p_pte_base + i;
        if (!(*p_pte & PTE_VALID))
            continue;

        // If at the last level, detach the mapped page.
        // Otherwise, recursively free the next level.
        if (level == 3)
            detach_mapped_page(p_pte);
        else
            __free_page_table_level(p_pte, level + 1);
    }

    // Release the memory for the PTE page.
    kfree_page((void *)p_pte_base);
}

void free_page_table(pgtbl_entry_t **pt) {
    ASSERT(*pt != NULL);
    __free_page_table_level((pgtbl_entry_t *)pt, 0);
    *pt = NULL;
}

void set_page_table(pgtbl_entry_t *pt) {
    extern PTEntries invalid_pt;
    if (pt)
        arch_set_ttbr0((u64)pt);
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void map_in_pgtbl(pgtbl_entry_t *pt, u64 va, void *ka, u64 flags) {
    pgtbl_entry_t *pte = get_pte(pt, va, true);
    *pte = (pgtbl_entry_t)(K2P(ka) | flags);
    struct page *page_mapped = get_page_info_by_kaddr(ka);
    if (page_mapped)
        increment_rc(&page_mapped->ref);
    arch_tlbi_vmalle1is();

#ifdef PT_DEBUG
    printk("[map_in_pgtbl]: va: %lld -> ka: %p | rc: %lld\n", va, ka,
           page_mapped->ref.count);
#endif
}

void unmap_in_pgtbl(pgtbl_entry_t *pt, u64 va) {
    pgtbl_entry_t *pte = get_pte(pt, va, false);
    if (!pte || !((u64)pte & PTE_VALID))
        return;

    detach_mapped_page(pte);

    *pte &= !PTE_VALID;

#ifdef PT_DEBUG
    printk("[unmap_in_pgtbl]: va: %lld -> ka: %p | rc: %lld\n", va,
           (void *)page_addr_k, page_mapped->ref.count);
#endif

    arch_tlbi_vmalle1is();
}

void unmap_range_in_pgtbl(pgtbl_entry_t *pt, u64 begin, u64 end) {
    ASSERT(begin % PAGE_SIZE == 0);
    ASSERT(end % PAGE_SIZE == 0);
    for (; begin < end; begin += PAGE_SIZE)
        unmap_in_pgtbl(pt, begin);
}

void __freeze_page_table_level(pgtbl_entry_t *pt, u8 level) {
    ASSERT(level <= 3);

    // Get the pointer to the first PTE in the current PTE page.
    pgtbl_entry_t *p_pte_base = (pgtbl_entry_t *)P2K(PTE_ADDRESS(*pt));

    // Iterate over each PTE in the table.
    for (int i = 0; i < N_PTE_PER_TABLE; i++) {
        pgtbl_entry_t *p_pte = p_pte_base + i;
        if (!(*p_pte & PTE_VALID))
            continue;

        // If not at the last level, recursively free the next level.
        if (level != 3)
            __freeze_page_table_level(p_pte, level + 1);

        *p_pte = (*p_pte) | PTE_RO;
        arch_tlbi_vmalle1is();
    }
}

void freeze_pgtbl(pgtbl_entry_t *pt) { __freeze_page_table_level(pt, 0); }

/* Copy len bytes from p to user address va in page table. */
int copy_to_user(pgtbl_entry_t* pt, void *va, void *p, usize len) {
    while (len > 0) {
        u64 p_addr = *get_pte(pt, (u64)va, true);
        if (p_addr == NULL)
            return -1;
        
        u64 offset = (u64)va - PAGE_BASE((u64)va);
        u64 n = PAGE_SIZE - offset;
        if (n > len)
            n = len;
        memcpy((void *)(P2K(PTE_ADDRESS(p_addr)) + offset), p, n);
        len -= n;
        p += n;
        va += n;
    }
    return 0;
}