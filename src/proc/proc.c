#include <fs/defines.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <lib/list.h>
#include <lib/printk.h>
#include <lib/string.h>
#include <proc/pid.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <vm/vmregion.h>
#include <vm/pgtbl.h>
#include <driver/clock.h>

struct proc root_proc;

struct spinlock proc_lock;

define_early_init(proc_d)
{
	init_spinlock(&proc_lock);
}

define_init(startup_procs)
{
	// Stage the idle processes
	for (int i = 0; i < NCPU; i++) {
		// Idle processes are the first processes on each core.
		// Before the idle process is staged, no other processes are created.
		ASSERT(cpus[i].sched.running == NULL);
		struct proc *idle = create_idle();
		cpus[i].sched.idle = idle;
		cpus[i].sched.running = idle;
		idle->state = RUNNING;
	}

	// Initialize the root process
	init_proc(&root_proc);
	root_proc.parent = &root_proc;
	start_proc(&root_proc, kernel_entry, 0);
}

void set_parent_to_this(struct proc *proc)
{
	ASSERT(proc->parent == NULL);
	acquire_spinlock(&proc_lock);
	proc->parent = thisproc();
	list_push_back(&(thisproc()->children), &(proc->ptnode));
	release_spinlock(&proc_lock);
}

static void transfer_children(struct proc *dst, struct proc *src)
{
	// Transfer the alive children to the destination process
	list_forall(pchild, src->children)
	{
		list_remove(&(src->children), pchild);
		list_push_back(&(dst->children), pchild);
		struct proc *child = container_of(pchild, struct proc, ptnode);
		child->parent = dst;
	}

	// Transfer the zombie children to the destination process
	list_forall(pzombie, src->zombie_children)
	{
		list_remove(&(src->zombie_children), pzombie);
		list_push_back(&(dst->zombie_children), pzombie);
		struct proc *zombie_child =
			container_of(pzombie, struct proc, ptnode);
		zombie_child->parent = dst;
		post_sem(&(dst->childexit));
	}
}

NO_RETURN void exit(int code)
{
	acquire_spinlock(&proc_lock);
	struct proc *p = thisproc();

	// Set the exit code.
	p->exitcode = code;

	// Free the page table.
	free_page_table(&(p->vmspace.pgtbl));

	// Transfer the children and zombies to the root proc.
	transfer_children(&root_proc, p);

	// Move the current process to the zombie children list of its parent.
	list_remove(&(p->parent->children), &(p->ptnode));
	list_push_back(&(p->parent->zombie_children), &(p->ptnode));

	for (int i = 0; i < NOFILE; ++i) {
		if (p->oftable.ofiles[i]) {
			file_close(p->oftable.ofiles[i]);
			p->oftable.ofiles[i] = NULL;
		}
	}
	struct op_ctx ctx;
	bcache.begin_op(&ctx);
	inodes.put(&ctx, p->cwd);
	p->cwd = NULL;
	release_spinlock(&proc_lock);
	bcache.end_op(&ctx);
	acquire_spinlock(&proc_lock);
	// Notify the parent.
	post_sem(&(p->parent->childexit));
	release_spinlock(&proc_lock);
	schedule(ZOMBIE);
	PANIC();
}

int wait(int *exitcode)
{
	acquire_spinlock(&proc_lock);
	struct proc *p = thisproc();

	// If the process has no children, then return.
	if ((!p->children.size) && (!p->zombie_children.size)) {
		release_spinlock(&proc_lock);
		return -1;
	}
	release_spinlock(&proc_lock);

	// Wait a child to exit.
	ASSERT(wait_sem(&(p->childexit)));

	// Fetch the zombie to clean the resources.
	acquire_spinlock(&proc_lock);

	// Take the zombie from the zombie list.
	ListNode *zombie = p->zombie_children.head;
	list_pop_head(&(p->zombie_children));
	struct proc *zombie_child = container_of(zombie, struct proc, ptnode);
	int pid = zombie_child->pid;
	*exitcode = zombie_child->exitcode;
	free_pid(zombie_child->pid);
	kfree_page(zombie_child->kstack);
	kfree(zombie_child);
	release_spinlock(&proc_lock);
	return pid;
}

int kill(int pid)
{
	// Set the killed flag of the proc to true and return 0.
	// Return -1 if the pid is invalid (proc not found).
	acquire_spinlock(&proc_lock);
	list_forall(p, thisproc()->children)
	{
		struct proc *cur = container_of(p, struct proc, ptnode);
		if (cur->pid == pid && !cur->idle) {
			cur->killed = true;
			alert_proc(cur);
			release_spinlock(&proc_lock);
			return 0;
		}
	}
	release_spinlock(&proc_lock);
	return -1;
}

int start_proc(struct proc *p, void (*entry)(u64), u64 arg)
{
	acquire_spinlock(&proc_lock);
	if (p->parent == NULL) {
		p->parent = &root_proc;
		list_push_back(&root_proc.children, &p->ptnode);
	}
	ASSERT(p->kstack != NULL);

	// Initialize the kernel context
	p->kcontext->regs[11] = (u64)&proc_entry;
	p->kcontext->x0 = (u64)entry;
	p->kcontext->x1 = (u64)arg;
	int pid = p->pid;
	activate_proc(p);
	release_spinlock(&proc_lock);
	return pid;
}

void init_proc(struct proc *p)
{
	acquire_spinlock(&proc_lock);
	memset(p, 0, sizeof(*p));
	p->killed = false;
	p->idle = false;
	p->pid = alloc_pid();
	p->state = UNUSED;
	init_sem(&p->childexit, 0);
	list_init(&p->children);
	list_init(&p->zombie_children);
	init_list_node(&p->ptnode);
	p->schinfo.runtime = 0;
	init_vmspace(&p->vmspace);
	p->kstack = kalloc_page();
	memset((void *)p->kstack, 0, PAGE_SIZE);
	p->kcontext = p->kstack + PAGE_SIZE - sizeof(KernelContext) -
		      sizeof(UserContext);
	p->ucontext = p->kstack + PAGE_SIZE - sizeof(UserContext);
	init_oftable(&p->oftable);
	release_spinlock(&proc_lock);
}

struct proc *create_proc()
{
	struct proc *p = kalloc(sizeof(struct proc));
	ASSERT(p != NULL);
	return p;
}

struct proc *create_idle()
{
	struct proc *p = create_proc();
	init_proc(p);
	p->parent = p;
	p->idle = true;
	start_proc(p, &idle_entry, 0);
	return p;
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
int fork()
{
	struct proc *this = thisproc();
	struct proc *child = create_proc();

	init_proc(child);
	set_parent_to_this(child);

	copy_vmspace(&this->vmspace, &child->vmspace, true);

	// Copy saved user registers.
	*(child->ucontext) = *(this->ucontext);

	// Cause fork to return 0 in the child.
	child->ucontext->regs[0] = 0;

	// Increment reference counts on open file descriptors.
	for (int i = 0; i < NOFILE; i++) {
		if (this->oftable.ofiles[i] != NULL)
			child->oftable.ofiles[i] =
				file_dup(this->oftable.ofiles[i]);
	}
	child->cwd = inodes.share(this->cwd);

	start_proc(child, trap_return, 0);
	arch_tlbi_vmalle1is();
	return child->pid;
}