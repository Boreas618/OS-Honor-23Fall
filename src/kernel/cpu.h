#pragma once

#include <lib/rbtree.h>
#include <proc/schinfo.h>
#include <kernel/param.h>

struct timer {
	bool triggered;
	int elapse;
	u64 _key;
	struct rb_node_ _node;
	void (*handler)(struct timer *);
	u64 data;
};

struct cpu {
	bool online;
	struct rb_root_ timer;
	struct sched sched;
};

extern struct cpu cpus[NCPU];

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);
