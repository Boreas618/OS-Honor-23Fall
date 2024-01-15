#include <lib/cond.h>
#include <lib/sem.h>
#include <lib/spinlock.h>

void cond_init(struct semaphore *cond) {
    init_sem(cond, 1);
}

/**
 * Atomically releases LOCK and waits for COND to be signaled by
 * some other piece of code.  After COND is signaled, LOCK is
 * reacquired before returning.  LOCK must be held before calling
 * this function.
 *
 * The monitor implemented by this function is "Mesa" style, not
 * "Hoare" style, that is, sending and receiving a signal are not
 * an atomic operation.  Thus, typically the caller must recheck
 * the condition after the wait completes and, if necessary, wait
 * again.
 *
 * A given condition variable is associated with only a single
 * lock, but one lock may be associated with any number of
 * condition variables.  That is, there is a one-to-many mapping
 * from locks to condition variables.
 *
 * This function may sleep, so it must not be called within an
 * interrupt handler.  This function may be called with
 * interrupts disabled, but interrupts will be turned back on if
 * we need to sleep.
 */
void cond_wait(struct semaphore *cond, struct spinlock *lock) {
    _lock_sem(cond);

    // If the semaphore is available, simply acquire it.
    if (_query_sem(cond) > 0) {
        ASSERT(_get_sem(cond));
        _unlock_sem(cond);
        return;
    }

    // Otherwise, release the lock, go to sleep, and reacquire the lock upon
    // waking up as a typical condition variable would do.
    release_spinlock(lock);
    ASSERT(_wait_sem(cond, false));
    acquire_spinlock(lock);
}

/**
 * If any threads are waiting on COND (protected by LOCK), then
 * this function signals one of them to wake up from its wait.
 * LOCK must be held before calling this function.
 *
 * An interrupt handler cannot acquire a lock, so it does not
 * make sense to try to signal a condition variable within an
 * interrupt handler.
 */
void cond_signal(struct semaphore *cond) {
    post_sem(cond);
}

/**
 * Wakes up all threads, if any, waiting on COND (protected by
 * LOCK).  LOCK must be held before calling this function.
 *
 * An interrupt handler cannot acquire a lock, so it does not
 * make sense to try to signal a condition variable within an
 * interrupt handler.
 */
void cond_broadcast(struct semaphore *cond) {
    post_all_sem(cond);
}