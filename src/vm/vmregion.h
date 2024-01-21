#pragma once

#include <aarch64/mmu.h>
#include <proc/proc.h>

#define ST_FILE 1
#define ST_SWAP (1 << 1)
#define ST_RO (1 << 2)
#define ST_HEAP (1 << 3)
#define ST_TEXT (ST_FILE | ST_RO)
#define ST_DATA ST_FILE
#define ST_BSS ST_FILE
#define ST_STACK (1 << 4)

struct vmregion {
    u64 flags;
    u64 begin;
    u64 end;
    struct list_node stnode;

    /* For file-backed sections. */
    struct file *fp;
    u64 offset;
};

void init_vmspace(struct vmspace *vms);
void copy_vmregions(struct vmspace *vms_source, struct vmspace *vms_dest);
void copy_vmspace(struct vmspace *vms_source, struct vmspace *vms_dest);
void destroy_vmspace(struct vmspace *vms);
struct vmregion *create_vmregion(struct vmspace *vms, u64 flags, u64 begin, u64 length);
int pgfault_handler(u64 iss);
void free_vmregions(struct vmspace *pd);
u64 sbrk(i64 size);