#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>
#include <common/list.h>
#include <common/string.h>

#define PID_POOL_SIZE 1<<20

extern ProcessQueues sched_queues;

struct proc root_proc;
SpinLock proc_lock;
SpinLock proc_pid_lock;
u8 pid_pool[PID_POOL_SIZE];
u64 pid_window;

void kernel_entry();
void idle_entry();
void proc_entry();
struct proc* _create_idle_proc();
void init_proc(struct proc* p);

define_early_init(proc_ds)
{
    init_spinlock(&proc_lock);
    init_spinlock(&proc_pid_lock);
}

define_init(startup_procs)
{
    // Stage the idle entries
    for(int i = 0; i < NCPU; i++) {
        // Idle processes are the first processes on each core.
        // Before the idle process is staged, no other processes are created/
        ASSERT(sched_queues.running[i] == NULL);
        struct proc* idle = _create_idle_proc();
        sched_queues.running[i] = idle;
        idle->state = RUNNING;
    }

    // Initialize the root process
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

int _alloc_pid() {
    setup_checker(0);
    acquire_spinlock(0, &proc_pid_lock);
    u64 old_pid_window = pid_window;

    while((pid_pool[pid_window % PID_POOL_SIZE]) & 0xFF) {
        pid_window++;
        if (pid_window == old_pid_window) {
            release_spinlock(0, &proc_pid_lock);
            return -1;
        }
    }

    for(int i = 0; i < 8; i++)
        if (pid_pool[pid_window % PID_POOL_SIZE] ^ (1 << i)) {
            release_spinlock(0, &proc_pid_lock);
            return (int)(pid_window * 8 + i);
        }
    release_spinlock(0, &proc_pid_lock);
    return -1;
}

void _free_pid(int pid) {
    setup_checker(0);
    acquire_spinlock(0, &proc_pid_lock);
    u32 index = (u32)(pid / 8);
    u8 offset = (u8)(pid % 8);
    pid_pool[index] |= 1 << offset;
    release_spinlock(0, &proc_pid_lock);
}

void set_parent_to_this(struct proc* proc)
{
    ASSERT(proc->parent == NULL);
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    proc->parent = thisproc();
    release_spinlock(0, &proc_lock);
}

NO_RETURN void exit(int code)
{
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    struct proc* p = thisproc();

    // Set exit code
    p->exitcode = code;

    // Transfer children and zombies to the root proc
    ListNode* pchild = p->children.next;
    while(pchild != &(p->children)) {
        _insert_into_list(&(root_proc.children), pchild);
        struct proc* child = container_of(pchild, struct proc, ptnode);
        child->parent = &root_proc;
        pchild = pchild->next;
    }

    ListNode* pzombie = p->zombie_children.next;
    while(pzombie != &(p->zombie_children)) {
        _insert_into_list(&(root_proc.zombie_children), pzombie);
        struct proc* zombie = container_of(pzombie, struct proc, ptnode);
        zombie->parent = &root_proc;
        post_sem(&root_proc.childexit);
        pzombie = pzombie->next;
    }

    _detach_from_list(&(p->parent->ptnode));
    _insert_into_list(&(p->parent->zombie_children), &(p->ptnode));
    post_sem(&(p->parent->childexit));
    release_spinlock(0, &proc_lock);

    _sched(ZOMBIE);
    PANIC();
}

int wait(int* exitcode)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    struct proc* p = thisproc();

    if (p->children.next == (&p->children) && p->zombie_children.next == (&p->zombie_children)) {
        release_spinlock(0, &proc_lock);
        return -1;
    }
    release_spinlock(0, &proc_lock);
    wait_sem(&p->childexit);
    setup_checker(1);
    acquire_spinlock(1, &proc_lock);
    ListNode* zombie = p->zombie_children.next;
    _detach_from_list(zombie);
    struct proc* zombie_child = container_of(zombie, struct proc, ptnode);
    int pid = zombie_child->pid;
    *exitcode = zombie_child->exitcode;
    _free_pid(zombie_child->pid);
    kfree_page(p->kstack);
    kfree(zombie_child);
    release_spinlock(1, &proc_lock);
    return pid;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    if (p->parent == NULL)
        p->parent = &root_proc;
    ASSERT(p->kstack != NULL);
    p->kcontext = p->kstack + PAGE_SIZE - sizeof(KernelContext);
    p->kcontext->csgp_regs[11] = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int pid = p->pid;
    activate_proc(p);
    return pid;
}

void init_proc(struct proc* p)
{
    setup_checker(0);
    acquire_spinlock(0, &proc_lock);
    p->killed = false;
    p->idle = false;
    p->pid = _alloc_pid();
    p->state = UNUSED;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->parent = NULL;
    init_schinfo(&p->schinfo);
    p->kstack = kalloc_page();
    p->ucontext = NULL;
    p->kcontext = NULL;
    release_spinlock(0, &proc_lock);
}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

struct proc* _create_idle_proc() {
    struct proc* p = create_proc();
    // Do some configurations to the idle entry
    p->idle = true;
    p->parent = p;
    start_proc(p, &idle_entry, 0);
    return p;
}