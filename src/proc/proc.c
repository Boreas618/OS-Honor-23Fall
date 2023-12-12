#include <lib/list.h>
#include <lib/string.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <proc/pid.h>

Proc root_proc;

SpinLock proc_lock;

extern u64 proc_entry();

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
        Proc *idle = create_idle_proc();
        cpus[i].sched.idle = idle;
        cpus[i].sched.running = idle;
        idle->state = RUNNING;
    }

    // Initialize the root process
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void 
set_parent_to_this(Proc *proc) 
{
    ASSERT(proc->parent == NULL);
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    proc->parent = thisproc();
    list_push_back(&(thisproc()->children), &(proc->ptnode));
    release_spinlock(0, &proc_lock);
}

static void 
transfer_children(Proc *dst, Proc *src) 
{
    // Transfer the alive children to the destination process
    list_forall(pchild, src->children) {
        list_remove(&(src->children), pchild);
        list_push_back(&(dst->children), pchild);
        Proc *child = container_of(pchild, Proc, ptnode);
        child->parent = dst;
    }

    // Transfer the zombie children to the destination process
    list_forall(pzombie, src->zombie_children) {
        list_remove(&(src->zombie_children), pzombie);
        list_push_back(&(dst->zombie_children), pzombie);
        Proc *zombie_child = container_of(pzombie, Proc, ptnode);
        zombie_child->parent = dst;
        post_sem(&(dst->childexit));
    }
}

NO_RETURN void 
exit(int code) 
{
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    Proc *p = thisproc();

    // Set the exit code.
    p->exitcode = code;

    // Free the page table.
    free_pgdir(&(p->pgdir));

    // Transfer the children and zombies to the root proc.
    transfer_children(&root_proc, p);

    // Move the current process to the zombie children list of its parent.
    list_remove(&(p->parent->children), &(p->ptnode));
    list_push_back(&(p->parent->zombie_children), &(p->ptnode));

    // Notify the parent.
    post_sem(&(p->parent->childexit));
    release_spinlock(0, &proc_lock);
    _sched(ZOMBIE);
    PANIC();
}

int 
wait(int *exitcode) 
{
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    Proc *p = thisproc();

    // If the process has no children, then return.
    if ((!p->children.size) && (!p->zombie_children.size)) {
        release_spinlock(0, &proc_lock);
        return -1;
    }
    release_spinlock(0, &proc_lock);

    // Wait a child to exit.
    bool r = wait_sem(&(p->childexit));
    if (!r)
        PANIC();

    // Fetch the zombie to clean the resources.
    setup_checker(1);
    acquire_spinlock(1, &proc_lock);

    // Take the zombie from the zombie list.
    ListNode *zombie = p->zombie_children.head;
    list_pop_head(&(p->zombie_children));
    Proc *zombie_child = container_of(zombie, Proc, ptnode);
    int pid = zombie_child->pid;
    *exitcode = zombie_child->exitcode;
    free_pid(zombie_child->pid);
    kfree_page(zombie_child->kstack);
    kfree(zombie_child);
    release_spinlock(1, &proc_lock);
    return pid;
}

int 
kill(int pid) 
{
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    list_forall(p, thisproc()->children) {
        Proc *cur = container_of(p, Proc, ptnode);
        if (cur->pid == pid && !cur->idle) {
            cur->killed = true;
            alert_proc(cur);
            release_spinlock(0, &proc_lock);
            return 0;
        }
    }
    release_spinlock(0, &proc_lock);
    return -1;
}

int 
start_proc(Proc *p, void (*entry)(u64), u64 arg) 
{
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    if (p->parent == NULL) {
        p->parent = &root_proc;
        list_push_back(&root_proc.children, &p->ptnode);
    }
    ASSERT(p->kstack != NULL);

    // Initialize the kernel context
    p->kcontext->csregs[11] = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int pid = p->pid;
    activate_proc(p);
    release_spinlock(0, &proc_lock);
    return pid;
}

void 
init_proc(Proc *p) 
{
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    memset(p, 0, sizeof(*p));
    p->killed = false;
    p->idle = false;
    p->pid = alloc_pid();
    p->state = UNUSED;
    init_sem(&p->childexit, 0);
    list_init(&p->children);
    list_init(&p->zombie_children);
    init_list_node(&p->ptnode);
    p->parent = NULL;
    p->schinfo.runtime = 0;
    init_pgdir(&p->pgdir);
    p->kstack = kalloc_page();
    ASSERT(p->kstack != NULL);
    memset((void *)p->kstack, 0, PAGE_SIZE);
    p->kcontext =
        p->kstack + PAGE_SIZE - sizeof(KernelContext) - sizeof(UserContext);
    p->ucontext = p->kstack + PAGE_SIZE - sizeof(UserContext);
    release_spinlock(0, &proc_lock);
}

Proc *
create_proc() 
{
    Proc *p = kalloc(sizeof(Proc));
    ASSERT(p != NULL);
    init_proc(p);
    return p;
}

Proc *
create_idle_proc() 
{
    Proc *p = create_proc();
    // Do some configurations to the idle entry
    p->idle = true;
    p->parent = p;
    start_proc(p, &idle_entry, 0);
    return p;
}