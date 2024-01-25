#include <aarch64/mmu.h>
#include <kernel/mem.h>
#include <lib/string.h>
#include <proc/sched.h>
#include <vm/vmregion.h>
#include <vm/mmap.h>

void handle_copy_on_write(pgtbl_entry_t *pte, struct vmspace *vs, u64 addr)
{
	void *target_page = (void *)P2K(PTE_ADDRESS(*pte));
	void *new_page = kalloc_page();
	memcpy(new_page, target_page, PAGE_SIZE);
	map_in_pgtbl(vs->pgtbl, addr, new_page, PTE_FLAGS(*pte) & (~PTE_RO));
	return;
}

void handle_memory_map(u64 begin, u64 end, struct vmspace *vs, u64 offset,
		       struct file *f, int prot)
{
	int pte_flag = PTE_USER_DATA;

	if (prot & PROT_READ)
		pte_flag |= PTE_RO;

	int length = end - begin;
	for (u64 i = begin; i < end; i += PAGE_SIZE)
		map_in_pgtbl(vs->pgtbl, i, kalloc_page(), PTE_USER_DATA);

	char *buf = (char *)kalloc_page();

	inodes.lock(f->ip);
	for (int i = 0; i < length; i += PAGE_SIZE) {
		memset(buf, 0, PAGE_SIZE);
		inodes.read(f->ip, (u8 *)buf, offset, PAGE_SIZE);
		copy_to_user(vs->pgtbl, (void *)(begin + i), (void *)buf,
			     MAX((usize)PAGE_SIZE, length - offset));
		offset += PAGE_SIZE;
	}
	inodes.unlock(f->ip);

	kfree_page((void *)buf);
}

int pgfault_handler(u64 iss)
{
	(void)iss;
	struct proc *p = thisproc();
	struct vmspace *vs = &p->vmspace;
	u64 addr = arch_get_far(); // The address which caused the page fault.
	pgtbl_entry_t *pte = get_pte(vs->pgtbl, addr, false);

	list_forall(p, vs->vmregions)
	{
		struct vmregion *v = container_of(p, struct vmregion, stnode);
		if (v->begin <= addr && addr < v->end) {
			bool vmr_read_only = (v->flags & VMR_RO);
			bool page_read_only = (PTE_FLAGS(*pte) & PTE_RO);
			bool is_vmr_mm = v->flags & VMR_MM;

			// Copy on Write.
			if (!vmr_read_only && page_read_only) {
				handle_copy_on_write(pte, vs, addr);
				goto finished;
			}

			// Memory mapped files.
			if (is_vmr_mm) {
				handle_memory_map(v->begin, v->end, vs,
						  v->mmap_info.offset,
						  v->mmap_info.fp, v->mmap_info.prot);
				goto finished;
			}
		}
	}

finished:
	// We have modified the page table. Therefore, we should flush TLB to ensure
	// consistency.
	arch_tlbi_vmalle1is();
	return 0;
}