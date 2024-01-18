#include <kernel/mem.h>
#include <kernel/syscall.h>
#include <lib/printk.h>
#include <lib/rc.h>
#include <lib/sem.h>
#include <proc/proc.h>
#include <test/test.h>
#include <vm/pt.h>
#include <vm/paging.h>

#define NPROC 512

extern struct proc *running[];
extern ListNode runnable;

void vm_test() {
    printk("vm_test\n");
    static void *p[100000];
    extern RefCount alloc_page_cnt;
    struct vmspace pg;
    int p0 = alloc_page_cnt.count;
    init_vmspace(&pg, NULL);
    for (u64 i = 0; i < 100000; i++) {
        p[i] = kalloc_page();
        *get_pte(pg.pgtbl, i << 12, true) = K2P(p[i]) | PTE_USER_DATA;
        *(int *)p[i] = i;
    }
    set_page_table(pg.pgtbl);
    for (u64 i = 0; i < 100000; i++) {
        ASSERT(*(int *)(P2K(PTE_ADDRESS(*get_pte(pg.pgtbl, i << 12, false)))) ==
               (int)i);
        ASSERT(*(int *)(i << 12) == (int)i);
    }
    free_page_table(&pg.pgtbl);
    set_page_table(pg.pgtbl);
    for (u64 i = 0; i < 100000; i++)
        kfree_page(p[i]);
    ASSERT(alloc_page_cnt.count == p0);
    printk("vm_test PASS\n");
}

void trap_return();

static u64 proc_cnt[NPROC], cpu_cnt[4];
static Semaphore myrepot_done;

define_syscall(myreport, u64 id) {
    static bool stop;
    ASSERT(id < NPROC);
    if (stop)
        return 0;
    proc_cnt[id]++;
    cpu_cnt[cpuid()]++;
    if (proc_cnt[id] > 100) {
        stop = true;
        post_sem(&myrepot_done);
    }
    return 0;
}

void user_proc_test() {
    printk("user_proc_test\n");
    init_sem(&myrepot_done, 0);
    extern char loop_start[], loop_end[];
    int pids[NPROC];
    for (int i = 0; i < NPROC; i++) {
        auto p = create_proc();
        init_proc(p, false, NULL);
        for (u64 q = (u64)loop_start; q < (u64)loop_end; q += PAGE_SIZE) {
            *get_pte(p->vmspace.pgtbl, 0x400000 + q - (u64)loop_start, true) =
                K2P(q) | PTE_USER_DATA;
        }
        ASSERT(p->vmspace.pgtbl);
        p->ucontext->regs[0] = i;
        p->ucontext->elr = 0x400000;
        p->ucontext->spsr = 0;
        pids[i] = start_proc(p, trap_return, 0);
        printk("pid[%d] = %d\n", i, pids[i]);
    }
    ASSERT(wait_sem(&myrepot_done));
    printk("done\n");
    for (int i = 0; i < NPROC; i++)
        ASSERT(kill(pids[i]) == 0);
    for (int i = 0; i < NPROC; i++) {
        int code;
        int pid = wait(&code);
        printk("pid %d killed\n", pid);
        ASSERT(code == -1);
    }
    printk("user_proc_test PASS\nRuntime:\n");
    for (int i = 0; i < 4; i++)
        printk("CPU %d: %llu\n", i, cpu_cnt[i]);
    for (int i = 0; i < NPROC; i++)
        printk("Proc %d: %llu\n", i, proc_cnt[i]);
}
