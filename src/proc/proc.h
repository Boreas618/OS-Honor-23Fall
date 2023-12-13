#pragma once

#include <lib/defines.h>
#include <lib/list.h>
#include <lib/sem.h>
#include <proc/schinfo.h>
#include <kernel/pt.h>

typedef struct uctx UserContext;
typedef struct kctx KernelContext;
typedef struct proc Proc;

extern struct proc root_proc;

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

struct uctx {
    u64 spsr;
    u64 elr;
    u64 sp;
    // General Purpose registers
    u64 gregs[31];
};

typedef struct kctx {
    u64 x0; 
    u64 x1;
    // Callee-saved General Purpose registers
    u64 csregs[12];
} KernelContext;

struct proc {
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    List children;
    List zombie_children;
    ListNode ptnode;
    struct proc* parent;
    struct schinfo schinfo;
    struct vmspace vmspace;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
};

void kernel_entry();
void idle_entry();
struct proc* create_idle_proc();
void init_proc(struct proc* p);
WARN_RESULT struct proc* create_proc();
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int* exitcode);
WARN_RESULT int kill(int pid);
