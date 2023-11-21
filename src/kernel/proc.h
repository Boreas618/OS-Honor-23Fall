#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext
{
    u64 spsr;
    u64 elr;
    u64 sp;
    // General Purpose registers
    u64 gp_regs[31];
} UserContext;

typedef struct KernelContext
{
    u64 x0; 
    u64 x1;
    // Callee-saved General Purpose registers
    u64 csgp_regs[12];
} KernelContext;

struct proc
{
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
    struct pgdir pgdir;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
};

WARN_RESULT struct proc* create_proc();
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int* exitcode);
WARN_RESULT int kill(int pid);
