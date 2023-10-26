#include <common/sem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context) {
  // TODO
  // Invoke syscall_table[id] with args and set the return value.
  // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
  u64 id = context->gp_regs[8];

  u64 arg0 = context->gp_regs[0];
  u64 arg1 = context->gp_regs[1];
  u64 arg2 = context->gp_regs[2];
  u64 arg3 = context->gp_regs[3];
  u64 arg4 = context->gp_regs[4];
  u64 arg5 = context->gp_regs[5];

  (void)((id < NR_SYSCALL) && (syscall_table[id] != NULL) &&
         (context->gp_regs[0] =
              ((u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
                  arg0, arg1, arg2, arg3, arg4, arg5)));
}
