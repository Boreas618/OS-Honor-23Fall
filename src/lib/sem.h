#pragma once

#include <lib/list.h>

#define lock_sem(checker, sem) \
	checker_begin_ctx_before_call(checker, _lock_sem, sem)

#define unlock_sem(checker, sem) \
	checker_end_ctx_after_call(checker, _unlock_sem, sem)

#define prelocked_wait_sem(checker, sem) \
	checker_end_ctx_after_call(checker, _wait_sem, sem, true)

#define prelocked_unalertable_wait_sem(checker, sem) \
	ASSERT(checker_end_ctx_after_call(checker, _wait_sem, sem, false))

#define wait_sem(sem) (_lock_sem(sem), _wait_sem(sem, true))

#define unalertable_wait_sem(sem) \
	ASSERT((_lock_sem(sem), _wait_sem(sem, false)))

#define post_sem(sem) (_lock_sem(sem), _post_sem(sem), _unlock_sem(sem))

#define get_sem(sem)                        \
	({                                  \
		_lock_sem(sem);             \
		bool __ret = _get_sem(sem); \
		_unlock_sem(sem);           \
		__ret;                      \
	})

#define SleepLock Semaphore

#define init_sleeplock(lock) init_sem(lock, 1)

#define acquire_sleeplock(checker, lock) \
	(checker_begin_ctx(checker), wait_sem(lock))

#define unalertable_acquire_sleeplock(checker, lock) \
	(checker_begin_ctx(checker), unalertable_wait_sem(lock))

#define release_sleeplock(checker, lock) \
	(post_sem(lock), checker_end_ctx(checker))

struct wait_data {
	bool up;
	struct proc *proc;
	struct list_node slnode;
};

typedef struct wait_data WaitData;

struct semaphore {
	struct spinlock lock;
	int val;
	struct list_node sleeplist;
};

typedef struct semaphore Semaphore;

void init_sem(Semaphore *, int val);
int get_all_sem(Semaphore *);
int post_all_sem(Semaphore *);
bool _get_sem(Semaphore *);
WARN_RESULT int _query_sem(Semaphore *);
void _lock_sem(Semaphore *);
void _unlock_sem(Semaphore *);
WARN_RESULT bool _wait_sem(Semaphore *, bool alertable);
void _post_sem(Semaphore *);
