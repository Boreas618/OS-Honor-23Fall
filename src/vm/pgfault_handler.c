#include <aarch64/mmu.h>
#include <kernel/mem.h>
#include <lib/string.h>
#include <proc/sched.h>
#include <vm/vmregion.h>

void handle_copy_on_write(pgtbl_entry_t *pte, struct vmspace* vs, u64 addr)
{
	void *target_page = (void *)P2K(PTE_ADDRESS(*pte));
	void *new_page = kalloc_page();
	memcpy(new_page, target_page, PAGE_SIZE);
	map_in_pgtbl(vs->pgtbl, addr, new_page, PTE_FLAGS(*pte) & (~PTE_RO));
	return;
}

int pgfault_handler(u64 iss)
{
	(void)iss;
	struct proc *p = thisproc();
	struct vmspace *vs = &p->vmspace;

	/* The address which caused the page fault. */
	u64 addr = arch_get_far();

	pgtbl_entry_t *pte = get_pte(vs->pgtbl, addr, false);

	list_forall(p, vs->vmregions)
	{
		struct vmregion *v = container_of(p, struct vmregion, stnode);
		if (v->begin <= addr && addr < v->end) {
			bool vmr_read_only = (v->flags & VMR_RO);
			bool page_read_only = (PTE_FLAGS(*pte) & PTE_RO);

            // Copy on Write.
			if (!vmr_read_only && page_read_only) {
				handle_copy_on_write(pte, vs, addr);
				goto finished;
			}
		}
	}
	ASSERT(1 == 2);

finished:
	// We have modified the page table. Therefore, we should flush TLB to ensure
	// consistency.
	arch_tlbi_vmalle1is();
	return 0;
}