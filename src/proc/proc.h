#pragma once

#include <fs/file.h>
#include <fs/inode.h>
#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <proc/schinfo.h>
#include <vm/pt.h>

typedef struct uctx UserContext;
typedef struct kctx KernelContext;
typedef struct proc Proc;

extern struct proc root_proc;

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

struct uctx {
    u64 pad; // In order to make the ctx 16-byte aligned.
    u64 q0;
    u64 elr;
    u64 tpidr0;
    u64 sp;
    u64 spsr;
    u64 regs[32]; // General purpose registers.
};

typedef struct kctx {
    u64 x0;
    u64 x1;
    u64 regs[12]; // Callee-saved general purpose registers.
} KernelContext;

struct proc {
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    void *kstack;
    enum procstate state;
    struct lock *lock;
    struct semaphore childexit;
    struct list children;
    struct list zombie_children;
    struct list_node ptnode;
    struct proc *parent;
    struct schinfo schinfo;
    struct vmspace vmspace;
    struct uctx *ucontext;
    struct kctx *kcontext;
    struct oftable oftable;
    struct inode *cwd;
};

void kernel_entry();
void idle_entry();
struct proc *create_idle_proc();
void init_proc(struct proc *p, bool idle, struct proc *parent);
WARN_RESULT struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode);
bool sleep(struct semaphore *sem, struct spinlock *lock);
WARN_RESULT int kill(int pid);
WARN_RESULT int fork();
void trap_return();