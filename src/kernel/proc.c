#include <common/list.h>
#include <common/string.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

#define PID_POOL_SIZE 1 << 20

struct proc root_proc;
SpinLock proc_lock;
SpinLock proc_pid_lock;
u8 pid_pool[PID_POOL_SIZE];
u64 pid_window;
extern u64 proc_entry();

define_early_init(proc_helper) {
  init_spinlock(&proc_lock);
  init_spinlock(&proc_pid_lock);
}

define_init(startup_procs) {
  // Stage the idle processes
  for (int i = 0; i < NCPU; i++) {
    // Idle processes are the first processes on each core.
    // Before the idle process is staged, no other processes are created.
    ASSERT(cpus[i].sched.running == NULL);
    struct proc* idle = create_idle_proc();
    cpus[i].sched.idle = idle;
    cpus[i].sched.running = idle;
    idle->state = RUNNING;
  }

  // Initialize the root process
  init_proc(&root_proc);
  root_proc.parent = &root_proc;
  start_proc(&root_proc, kernel_entry, 123456);
}

static int alloc_pid() {
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

static void free_pid(int pid) {
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
  list_push_back(&(thisproc()->children), &(proc->ptnode));
  release_spinlock(0, &proc_lock);
}

static void transfer_children(struct proc* dst, struct proc* src) {
  // Transfer the alive children to the destination process
  list_forall(pchild, src->children) {
    list_remove(&(src->children), pchild);
    list_push_back(&(dst->children), pchild);
    struct proc* child = container_of(pchild, struct proc, ptnode);
    child->parent = dst;
  }

  // Transfer the zombie children to the destination process
  list_forall(pzombie, src->zombie_children) {
    list_remove(&(src->zombie_children), pzombie);
    list_push_back(&(dst->zombie_children), pzombie);
    struct proc* zombie_child = container_of(pzombie, struct proc, ptnode);
    zombie_child->parent = dst;
    post_sem(&(dst->childexit));
  }
}

NO_RETURN void exit(int code) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  struct proc* p = thisproc();

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

int wait(int* exitcode) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  struct proc* p = thisproc();

  // If the process has no children, then return.
  if ((!p->children.size) &&
      (!p->zombie_children.size)) {
    release_spinlock(0, &proc_lock);
    return -1;
  }
  release_spinlock(0, &proc_lock);

  // Wait a child to exit.
  bool r = wait_sem(&(p->childexit));
  if(!r) PANIC();

  // Fetch the zombie to clean the resources.
  setup_checker(1);
  acquire_spinlock(1, &proc_lock);

  // Take the zombie from the zombie list.
  ListNode* zombie = p->zombie_children.head;
  list_pop_head(&(p->zombie_children));
  struct proc* zombie_child = container_of(zombie, struct proc, ptnode);
  int pid = zombie_child->pid;
  *exitcode = zombie_child->exitcode;
  free_pid(zombie_child->pid);
  kfree_page(zombie_child->kstack);
  kfree(zombie_child);
  release_spinlock(1, &proc_lock);
  return pid;
}

int kill(int pid) {
  // Set the killed flag of the proc to true and return 0.
  // Return -1 if the pid is invalid (proc not found).
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  list_forall(p, thisproc()->children) {
    struct proc *cur = container_of(p, struct proc, ptnode);
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

int start_proc(struct proc* p, void (*entry)(u64), u64 arg) {
  setup_checker(0);
  acquire_spinlock(0, &proc_lock);
  if (p->parent == NULL) {
    p->parent = &root_proc;
    list_push_back(&root_proc.children, &p->ptnode);
  }
  ASSERT(p->kstack != NULL);

  // Initialize the kernel context
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
  memset((void*)p->kstack, 0, PAGE_SIZE);
  p->kcontext = p->kstack + PAGE_SIZE - sizeof(KernelContext) - sizeof(UserContext);
  p->ucontext = p->kstack + PAGE_SIZE - sizeof(UserContext);
  release_spinlock(0, &proc_lock);
}

struct proc* create_proc() {
  struct proc* p = kalloc(sizeof(struct proc));
  ASSERT(p != NULL);
  init_proc(p);
  return p;
}

struct proc* create_idle_proc() {
  struct proc* p = create_proc();
  // Do some configurations to the idle entry
  p->idle = true;
  p->parent = p;
  start_proc(p, &idle_entry, 0);
  return p;
}