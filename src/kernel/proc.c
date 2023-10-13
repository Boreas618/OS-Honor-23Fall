#include <common/list.h>
#include <common/string.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

#define PID_POOL_SIZE 1 << 20

extern struct proc* running[NCPU];
extern struct proc* idle[NCPU];

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

define_early_init(proc_ds) {
  init_spinlock(&proc_lock);
  init_spinlock(&proc_pid_lock);
}

define_init(startup_procs) {
  // Stage the idle entries
  for (int i = 0; i < NCPU; i++) {
    // Idle processes are the first processes on each core.
    // Before the idle process is staged, no other processes are created/
    ASSERT(running[i] == NULL);
    struct proc* idle = _create_idle_proc();
    running[i] = idle;
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

  while (pid_pool[pid_window] == 0xFF) {
    pid_window++;
    if (pid_window == PID_POOL_SIZE)
      pid_window = 0;
    if (pid_window == old_pid_window) {
      release_spinlock(0, &proc_pid_lock);
      return -1;
    }
  }

  for (int i = 0; i < 8; i++)
    if ((pid_pool[pid_window] ^ (1 << i)) & (1 << i)) {
      pid_pool[pid_window] |= (1 << i);
      int pid = (int)(pid_window * 8 + i);
      release_spinlock(0, &proc_pid_lock);
      return pid;
    }
  release_spinlock(0, &proc_pid_lock);
  return -1;
}

void _free_pid(int pid) {
  setup_checker(0);
  acquire_spinlock(0, &proc_pid_lock);
  u32 index = (u32)(pid / 8);
  u8 offset = (u8)(pid % 8);
  pid_pool[index] &= ~(1 << (u8)offset);
  release_spinlock(0, &proc_pid_lock);
}

void set_parent_to_this(struct proc* proc) {
  ASSERT(proc->parent == NULL);
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  proc->parent = thisproc();
  _insert_into_list(&(thisproc()->children), &(proc->ptnode));
  release_spinlock(0, &proc_lock);
}

void _transfer_children(struct proc* dst, struct proc* src) {
  // Transfer the alive children to the destination process
  while (src->children.next != &(src->children)) {
    ListNode *pchild = src->children.next;
    _detach_from_list(src->children.next);
    _insert_into_list(&(dst->children), pchild);
    struct proc* child = container_of(pchild, struct proc, ptnode);
    child->parent = dst;
  }

  // Transfer the zombie children to the destination process
  while (src->zombie_children.next != &(src->zombie_children)) {
    ListNode* pzombie = src->zombie_children.next;
    _detach_from_list(src->zombie_children.next);
    _insert_into_list(&(dst->zombie_children), pzombie);
    struct proc* zombie = container_of(pzombie, struct proc, ptnode);
    zombie->parent = dst;
    post_sem(&(dst->childexit));
  }
}

NO_RETURN void exit(int code) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  struct proc* p = thisproc();

  // Set exit code
  p->exitcode = code;

  // Transfer children and zombies to the root proc
  _transfer_children(&root_proc, p);

  // Move the current process to the zombie children list of its parent
  _detach_from_list(&(p->ptnode));
  _insert_into_list(&(p->parent->zombie_children), &(p->ptnode));
  post_sem(&(p->parent->childexit));
  release_spinlock(0, &proc_lock);

  _acquire_sched_lock();
  _sched(ZOMBIE);
  PANIC();
}

int wait(int* exitcode) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  struct proc* p = thisproc();

  // If the process has no children ,then return
  if ((p->children.next == &p->children) &&
      (p->zombie_children.next == &p->zombie_children)) {
    release_spinlock(0, &proc_lock);
    return -1;
  }
  release_spinlock(0, &proc_lock);

  // Wait a child to exit
  wait_sem(&p->childexit);

  // Fetch the zombie to clean the resources
  setup_checker(1);
  acquire_spinlock(1, &proc_lock);
  ListNode* zombie = p->zombie_children.next;
  _detach_from_list(zombie);
  struct proc* zombie_child = container_of(zombie, struct proc, ptnode);
  int pid = zombie_child->pid;
  *exitcode = zombie_child->exitcode;
  _free_pid(zombie_child->pid);
  kfree_page(zombie_child->kstack);
  kfree(zombie_child);
  release_spinlock(1, &proc_lock);
  return pid;
}

int start_proc(struct proc* p, void (*entry)(u64), u64 arg) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  if (p->parent == NULL) {
    p->parent = &root_proc;
    _insert_into_list(&root_proc.children, &p->ptnode);
  }
  ASSERT(p->kstack != NULL);

  // Initialize the kernel context
  p->kcontext = p->kstack + PAGE_SIZE - sizeof(KernelContext);
  p->kcontext->csgp_regs[11] = (u64)&proc_entry;
  p->kcontext->x0 = (u64)entry;
  p->kcontext->x1 = (u64)arg;
  int pid = p->pid;
  activate_proc(p);
  release_spinlock(0, &proc_lock);
  return pid;
}

void init_proc(struct proc* p) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  memset(p, 0, sizeof(*p));
  p->killed = false;
  p->idle = false;
  p->pid = _alloc_pid();
  p->state = UNUSED;
  init_sem(&p->childexit, 0);
  init_list_node(&p->children);
  init_list_node(&p->ptnode);
  init_list_node(&p->zombie_children);
  p->parent = NULL;
  init_schinfo(&p->schinfo);
  p->kstack = kalloc_page();
  ASSERT(p->kstack != NULL);
  p->ucontext = NULL;
  p->kcontext = NULL;
  release_spinlock(0, &proc_lock);
}

struct proc* create_proc() {
  struct proc* p = kalloc(sizeof(struct proc));
  ASSERT(p != NULL);
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