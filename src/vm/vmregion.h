#pragma once

#include <aarch64/mmu.h>
#include <proc/proc.h>

#define VMR_FILE 1
#define VMR_SWAP (1 << 1)
#define VMR_RO (1 << 2)
#define VMR_HEAP (1 << 3)
#define VMR_TEXT (VMR_FILE | VMR_RO)
#define VMR_DATA VMR_FILE
#define VMR_BSS VMR_FILE
#define VMR_STACK (1 << 4)

struct vmregion {
	u64 flags;
	u64 begin;
	u64 end;
	struct list_node stnode;

	/* For file-backed sections. */
	struct file *fp;
	u64 offset;
};

void freeze_pages_in_vmspace(struct vmspace *vms);
void init_vmspace(struct vmspace *vms);
void copy_vmregions(struct vmspace *vms_source, struct vmspace *vms_dest);
void copy_vmspace(struct vmspace *vms_source, struct vmspace *vms_dest, bool share);
void destroy_vmspace(struct vmspace *vms);
struct vmregion *create_vmregion(struct vmspace *vms, u64 flags, u64 begin,
				 u64 length);
void free_vmregions(struct vmspace *pd);
u64 sbrk(i64 size);
bool user_readable(const void *start, usize size);
bool user_writeable(const void *start, usize size);
usize user_strlen(const char *str, usize maxlen);