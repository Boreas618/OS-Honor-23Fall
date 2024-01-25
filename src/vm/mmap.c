#include <proc/sched.h>
#include <vm/mmap.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/string.h>

static void *mmap_get_addr(void *addr, int length, struct vmspace *vs)
{
	if (addr == 0) {
		u64 start = 0;
		list_forall(pv, vs->vmregions)
		{
			struct vmregion *vmr =
				container_of(pv, struct vmregion, stnode);
			if (!(vmr->flags & VMR_STACK) && (vmr->end > start))
				start = vmr->end;
		}
		return (void *)round_up((u64)start, PAGE_SIZE);
	} else {
		/**
         * WARNING: the addr is rounded up to make the start address of the 
         * mapping area page-aligned. Per System Calls Manual of mmap, on Linux, 
         * the kernel will pick a nearby page boundary (but always above or 
         * equal to the value specified by /proc/sys/vm/mmap_min_addr) 
         * and attempt to create the mapping there. 
         */
		u64 rounded_addr = round_up((u64)addr, PAGE_SIZE);
		if (!check_vmregion_intersection(vs, rounded_addr,
						 rounded_addr + length))
			return (void *)rounded_addr;
		else
			return 0;
	}
}

u64 mmap(void *addr, int length, int prot, int flags, int fd, usize offset)
{
	struct proc *p = thisproc();
	struct file *f = p->oftable.ofiles[fd];

	if ((prot & PROT_WRITE) && (!f->writable && !(flags & MAP_PRIVATE)))
		return -1;
	if ((prot & PROT_READ) && !f->readable)
		return -1;

	// int pte_flag = PTE_USER_DATA;

	struct vmregion *v = kalloc(sizeof(struct vmregion));
	v->flags = VMR_MM;
	v->mmap_info.flags = flags;
	v->mmap_info.offset = offset;
	v->mmap_info.fp = file_dup(f);
	v->mmap_info.prot = prot;

	v->begin = (u64)mmap_get_addr(addr, length, &p->vmspace);
	v->end = v->begin + length;

	if (v->begin == 0)
		goto bad;

	list_push_back(&p->vmspace.vmregions, &v->stnode);

	for (u64 i = v->begin; i < v->end; i += PAGE_SIZE) {
		pgtbl_entry_t *pte = get_pte(p->vmspace.pgtbl, i, true);
		if (pte == NULL)
			goto bad;
		*pte &= ~PTE_VALID;
	}

	return v->begin;

bad:
	kfree(v);
	return -1;
}

int munmap(void *addr, usize length)
{
	struct vmregion *v = NULL;
	struct proc *p = thisproc();

	list_forall(pv, p->vmspace.vmregions)
	{
		struct vmregion *vmr =
			container_of(pv, struct vmregion, stnode);
		if (vmr->begin <= (u64)addr && (u64)addr < vmr->end) {
			v = vmr;
			break;
		}
	}

	if (v == NULL)
		return -1;

	if (length < (v->end - v->begin))
		v->begin = (u64)addr + length;
	else if (length == (v->end - v->begin))
		list_remove(&p->vmspace.vmregions, &v->stnode);
	else
		return -1;

	if ((v->mmap_info.flags & MAP_SHARED)) {
		struct op_ctx ctx;
		bcache.begin_op(&ctx);
		inodes.lock(v->mmap_info.fp->ip);
		inodes.write(&ctx, v->mmap_info.fp->ip, (u8 *)addr,
			     v->mmap_info.offset, length);
		inodes.unlock(v->mmap_info.fp->ip);
		bcache.end_op(&ctx);
	} else {
		unmap_range_in_pgtbl(p->vmspace.pgtbl, (u64)addr,
				     (u64)addr + length);
	}

	if (length == (v->end - v->begin)) {
		file_close(v->mmap_info.fp);
		kfree(v);
	}

	return 0;
}