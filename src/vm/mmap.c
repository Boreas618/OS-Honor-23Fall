#include <proc/sched.h>
#include <vm/mmap.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/string.h>

u64 mmap(void *addr, int length, int prot, int flags, int fd, usize offset)
{
	struct proc *p = thisproc();
	struct file *f = p->oftable.ofiles[fd];

	if ((prot & PROT_WRITE) && (!f->writable && !(flags & MAP_PRIVATE)))
		return -1;
	if ((prot & PROT_READ) && !f->readable)
		return -1;

	int pte_flag = PTE_USER_DATA;

	struct vmregion *v = kalloc(sizeof(struct vmregion));
	v->flags = VMR_MM;
	v->mmap_info.flags = flags;
	v->mmap_info.offset = offset;
	v->mmap_info.fp = f;
	v->mmap_info.prot = prot;
	file_dup(f);

	if (addr == 0) {
		u64 start = 0;
		list_forall(pv, p->vmspace.vmregions)
		{
			struct vmregion *vmr =
				container_of(pv, struct vmregion, stnode);
			if (vmr->end > start)
				start = vmr->end;
		}
		v->begin = PAGE_BASE(start) + PAGE_SIZE;
	} else {
		if (!check_vmregion_intersection(&p->vmspace, (u64)addr,
						 (u64)addr + length))
			v->begin = (u64)addr;
	}
	v->end = v->begin + length;

	list_push_back(&p->vmspace.vmregions, &v->stnode);

	for (u64 i = v->begin; i < v->end; i += PAGE_SIZE) {
		map_in_pgtbl(p->vmspace.pgtbl, i, kalloc_page(), pte_flag);
	}

	char *buf = (char *)kalloc_page();

	inodes.lock(f->ip);
	int i = 0;
	while (i < length) {
		memset(buf, 0, PAGE_SIZE);
		inodes.read(f->ip, (u8 *)buf, offset, PAGE_SIZE);
		copy_to_user(p->vmspace.pgtbl, (void *)v->begin + i,
			     (void *)buf,
			     MAX((usize)PAGE_SIZE, length - offset));
		i += PAGE_SIZE;
		offset += PAGE_SIZE;
	}
	inodes.unlock(f->ip);

	kfree_page((void *)buf);

	return v->begin;
}

int munmap(void *addr, usize length)
{
	struct vmregion *v = NULL;
	struct proc *p = thisproc();

	list_forall(pv, p->vmspace.vmregions)
	{
		struct vmregion *vmr =
			container_of(pv, struct vmregion, stnode);
		if (vmr->begin == (u64)addr) {
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

	return 0;
}