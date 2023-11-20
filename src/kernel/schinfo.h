#pragma once

#include <common/list.h>
#include <common/rbtree.h>
struct proc; // Don't include proc.h here

struct sched
{
    struct proc* running;
    struct proc* idle;
};

struct schinfo
{
    ListNode runnable_node;
    struct rb_node_ rq_node;
    int sched_id;
    u64 runtime;
};