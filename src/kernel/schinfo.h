#pragma once

#include <lib/list.h>
#include <lib/rbtree.h>
struct proc; // Don't include proc.h here

struct sched {
    struct proc* running;
    struct proc* idle;
};

struct schinfo {
    struct rb_node_ rq_node;
    u64 runtime;
};