#include <kernel/cpu.h>
#include <kernel/init.h>
#include <lib/printk.h>
#include <proc/sched.h>
#include <test/test.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>
#include <driver/disk.h>

bool panic_flag;

extern char icode[];
extern char eicode[];

NO_RETURN void idle_entry()
{
	set_cpu_on();
	while (1) {
		yield();
		if (panic_flag)
			break;
		arch_with_trap
		{
			arch_wfi();
		}
	}
	set_cpu_off();
	arch_stop_cpu();
}

void set_user_init()
{
	struct proc *p = thisproc();

	// The working dir of the init process is '/'
	p->cwd = inodes.share(inodes.root);

	// Map init.S to 0x8000 in the process's memory space.
	create_vmregion(&p->vmspace, VMR_TEXT, 0x8000,
			(u64)eicode - PAGE_BASE((u64)icode));

	for (u64 i = PAGE_BASE((u64)(icode)); i <= (u64)eicode; i += PAGE_SIZE)
		map_in_pgtbl(p->vmspace.pgtbl,
			     0x8000 + (i - PAGE_BASE((u64)icode)), (void *)i,
			     PTE_USER_DATA | PTE_RO);

	// After trap_return, jump to 0x8000.
	p->ucontext->elr = (u64)0x8000 + (u64)icode - (PAGE_BASE((u64)icode));
}

void print_logo() {
    printk("\n");
	printk("   _______  _____\n");
	printk("  / __/ _ \\/ ___/__  _______\n");
	printk(" / _// // / /__/ _ \\/ __/ -_)\n");
	printk("/_/ /____/\\___/\\___/_/  \\__/\n");
	printk("\n");
}

void kernel_entry()
{
    print_logo();
	disk_init();
	// sd_test();
	// proc_test();
	// vm_test();
	// user_proc_test();
	// pgfault_first_test();
	// pgfault_second_test();
	do_rest_init();

	set_user_init();

	set_return_addr(trap_return);
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
	printk("[CPU %d PANIC] ", cpuid());
	panic_flag = true;
	set_cpu_off();
	for (int i = 0; i < NCPU; i++) {
		if (cpus[i].online)
			i--;
	}
	printk("Kernel PANIC invoked at %s:%d.\n", file, line);
	arch_stop_cpu();
}
