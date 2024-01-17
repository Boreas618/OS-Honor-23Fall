#include <aarch64/trap.h>
#include <elf.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/syscall.h>
#include <lib/defines.h>
#include <lib/string.h>
#include <proc/proc.h>
#include <proc/sched.h>
#include <vm/paging.h>
#include <vm/pt.h>

// static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[]) {
    // TODO
    (void)path;
    (void)argv;
    (void)envp;
    return 0;
}
