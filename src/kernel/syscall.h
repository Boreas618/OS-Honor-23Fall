#include <kernel/init.h>
#include <kernel/syscallno.h>
#include <proc/proc.h>

#define NR_SYSCALL 512

#define define_syscall(name, ...)                        \
	static u64 sys_##name(__VA_ARGS__);              \
	define_early_init(__syscall_##name)              \
	{                                                \
		syscall_table[SYS_##name] = &sys_##name; \
	}                                                \
	static u64 sys_##name(__VA_ARGS__)

extern void *syscall_table[NR_SYSCALL];

void syscall_entry(UserContext *context);
bool user_readable(const void *start, usize size);
bool user_writeable(const void *start, usize size);
usize user_strlen(const char *str, usize maxlen);