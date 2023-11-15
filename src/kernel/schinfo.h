#pragma once

#include <common/list.h>
struct proc; // dont include proc.h here

// embedded data for cpus
struct sched
{
    struct proc* running;
    struct proc* idle;
};

// embeded data for procs
struct schinfo
{
    ListNode runnable_node;
};