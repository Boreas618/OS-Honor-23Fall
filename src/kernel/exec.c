#include <aarch64/trap.h>
#include <elf.h>
#include <fs/defines.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/syscall.h>
#include <lib/defines.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>
#include <kernel/param.h>

static int execve_load_section(struct vmspace *vms, Elf64_Phdr ph,
			       struct inode *ip)
{
	u64 flags = 0;

	if (ph.p_flags == (PF_R | PF_X))
		flags = VMR_FILE | VMR_RO;
	else if (ph.p_flags == (PF_R | PF_W))
		flags = VMR_FILE;
	else
		return 0;

	struct vmregion *vmr =
		create_vmregion(vms, flags, ph.p_vaddr, ph.p_memsz);

	/**
     * ph.p_vaddr represents the starting address in the virtual memory
     * space where mapping will occur. The field ph.p_filesz specifies the
     * size of the content within the ELF file. Conversely, ph.p_memsz
     * indicates the size of the content when loaded into memory. It is
     * important to note that ph.p_memsz is always equal to or larger than
     * ph.p_filesz. The initial ph.p_filesz bytes in memory are filled with
     * the content from the ELF file. Any additional space, calculated as
     * (ph.p_memsz - ph.p_filesz), is initialized to zero.
     */

	u64 begin = ph.p_vaddr;
	u64 end = ph.p_vaddr + ph.p_filesz;

	while (begin < end) {
		// Allocate the physical page to be mapped.
		void *page_to_map = (void *)kalloc_page();
		if (page_to_map == NULL)
			return -1;
		memset(page_to_map, 0, PAGE_SIZE);

		// begin is not guranteed to be page aligned.
		u64 len = MIN(end - begin,
			      (u64)PAGE_SIZE - (begin - PAGE_BASE(begin)));
		u8 *dest =
			(u8 *)((u64)page_to_map + (begin - PAGE_BASE(begin)));
		usize offset = ph.p_offset + (begin - ph.p_vaddr);

		// Copy the content from ELF to memory.
		if (inodes.read(ip, dest, offset, len) < len)
			return -1;

		// Map the page in page table.
		u64 pte_flags = PTE_USER_DATA;
		if (vmr->flags & VMR_RO)
			pte_flags |= PTE_RO;

		map_in_pgtbl(vms->pgtbl, PAGE_BASE(begin), page_to_map,
			     pte_flags);

		begin += len;
	}

	// Fill the remaining space between ph.p_filesz and ph.p_memsz with zero.
	if (begin % PAGE_SIZE != 0)
		begin = PAGE_BASE(begin) + PAGE_SIZE;

	while (begin < ph.p_vaddr + ph.p_memsz) {
		void *ka = kalloc_page();
		memset(ka, 0, PAGE_SIZE);
		map_in_pgtbl(vms->pgtbl, begin, ka, PTE_USER_DATA);
		begin += PAGE_SIZE;
	}

	return 0;
}

static u64 execve_alloc_stack(struct vmspace *vms, u64 *sb)
{
	u64 stack_base = 0;
	list_forall(p, vms->vmregions)
	{
		struct vmregion *vmr = container_of(p, struct vmregion, stnode);
		if (vmr->end > stack_base)
			stack_base = vmr->end;
	}

	// There should be one protecting page for the stack.
	stack_base = PAGE_BASE(stack_base) + 2 * PAGE_SIZE;
	*sb = stack_base;
	u64 sp = stack_base + STACK_SIZE;

	struct vmregion *vmr =
		create_vmregion(vms, VMR_STACK, stack_base, STACK_SIZE);
	if (vmr == NULL)
		return -1;

	while (stack_base < sp) {
		void *ka = kalloc_page();
		memset(ka, 0, PAGE_SIZE);
		map_in_pgtbl(vms->pgtbl, stack_base, ka, PTE_USER_DATA);
		stack_base += PAGE_SIZE;
	}

	return sp;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
	struct op_ctx ctx;
	bcache.begin_op(&ctx);

	struct inode *ip = namei(path, &ctx);
	if (!ip) {
		bcache.end_op(&ctx);
		return -1;
	}

	inodes.lock(ip);

	// Read the elf header.
	Elf64_Ehdr elf;
	if (inodes.read(ip, (u8 *)&elf, 0, sizeof(Elf64_Ehdr)) <
	    sizeof(Elf64_Ehdr))
		goto bad;

	// Check the magic number.
	if (strncmp((const char *)elf.e_ident, ELFMAG, strlen(ELFMAG)) != 0)
		goto bad;

	// Read and parse program header.
	struct vmspace vms;
	init_vmspace(&vms);

	Elf64_Half off = elf.e_phoff;
	for (Elf64_Half i = 0; i < elf.e_phnum; i++) {
		// Read program header.
		Elf64_Phdr ph;
		if (inodes.read(ip, (u8 *)(&ph), off, sizeof(Elf64_Phdr)) <
		    sizeof(Elf64_Phdr))
			goto bad;

		if (ph.p_type != PT_LOAD)
			continue;

		ASSERT(ph.p_memsz >= ph.p_filesz);

		if (execve_load_section(&vms, ph, ip) < 0)
			goto bad;

		off += sizeof(Elf64_Phdr);
	}

	inodes.unlock(ip);
	inodes.put(&ctx, ip);
	bcache.end_op(&ctx);

	u64 stack_base = 0;
	u64 sp = execve_alloc_stack(&vms, &stack_base);

	char *ustack_envp[32] = { 0 };
	int envc = 0;
	if (envp != NULL) {
		for (envc = 0; envp[envc]; envc++) {
			if (envc > MAX_ENV)
				goto bad;
			// Make space for the environment variable string on the stack. The
			// starting address of the string is 16-byte aligned.
			sp -= strlen(envp[envc]) + 1;
			sp -= sp % 16;

			if (sp < stack_base)
				goto bad;
			if (copy_to_user(vms.pgtbl, (void *)(sp), envp[envc],
					 strlen(envp[envc]) + 1) < 0)
				goto bad;
			ustack_envp[envc] = (char *)sp;
		}
		ustack_envp[envc] = 0;
	}

	sp -= 16;
	char *ustack_argv[32] = { 0 };

	int argc = 0;
	if (argv != NULL) {
		for (argc = 0; argv[argc]; argc++) {
			if (argc > MAX_ARG)
				goto bad;
			// Make space for the environment variable string on the stack. The
			// starting address of the string is 16-byte aligned.
			sp -= strlen(argv[argc]) + 1;
			sp -= sp % 16;

			if (sp < stack_base)
				goto bad;
			if (copy_to_user(vms.pgtbl, (void *)(sp), argv[argc],
					 strlen(argv[argc]) + 1) < 0)
				goto bad;
			ustack_argv[argc] = (char *)sp;
		}
		ustack_argv[argc] = 0;
	}

	sp -= sp % 16;

	sp -= (envc + 1) * sizeof(char *);
	ASSERT(copy_to_user(vms.pgtbl, (void *)sp, &ustack_envp,
			    (envc + 1) * sizeof(char *)) == 0);

	sp -= (argc + 1) * sizeof(char *);
	ASSERT(copy_to_user(vms.pgtbl, (void *)sp, &ustack_argv,
			    (argc + 1) * sizeof(char *)) == 0);

	/**
     * See https://stackoverflow.com/questions/25589452/c-c-argv-memory-manage
     * 
     * On some platforms, the layout of memory is such that the number of
     * arguments (argc) is available, followed by the argument vector, followed
     * by the environment vector.
     *
     *          argv                            environ
     *            |                                |
     *            v                                v
     * | argc | argv0 | argv1 | ... | argvN | 0 | env0 | env1 | ... | envN | 0 |
     */

	sp -= 8;
	ASSERT(copy_to_user(vms.pgtbl, (void *)sp, &argc, sizeof(int)) == 0);

	thisproc()->ucontext->sp = sp;
	thisproc()->ucontext->elr = elf.e_entry;

	copy_vmregions(&vms, &thisproc()->vmspace);
	thisproc()->vmspace.pgtbl = vms.pgtbl;

	set_page_table(thisproc()->vmspace.pgtbl);

	return argc;
bad:
	destroy_vmspace(&vms);
	if (ip) {
		inodes.unlock(ip);
		inodes.put(&ctx, ip);
		bcache.end_op(&ctx);
	}
	return -1;
}