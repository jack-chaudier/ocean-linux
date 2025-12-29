/*
 * Ocean Kernel - Spinlock Implementation
 *
 * Simple ticket-based spinlock for SMP synchronization.
 */

#ifndef _OCEAN_SPINLOCK_H
#define _OCEAN_SPINLOCK_H

#include <ocean/types.h>
#include <ocean/defs.h>

/*
 * Ticket spinlock
 *
 * Fair spinlock using ticket system - threads acquire lock in FIFO order.
 * Prevents starvation that can occur with simple test-and-set locks.
 */
typedef struct spinlock {
    volatile u32 next_ticket;   /* Next ticket to be issued */
    volatile u32 now_serving;   /* Currently serving ticket */
} spinlock_t;

/* Static initializer */
#define SPINLOCK_INIT { .next_ticket = 0, .now_serving = 0 }

/* Declare and initialize a spinlock */
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

/* Initialize a spinlock at runtime */
static __always_inline void spin_init(spinlock_t *lock)
{
    lock->next_ticket = 0;
    lock->now_serving = 0;
}

/* Acquire spinlock */
static __always_inline void spin_lock(spinlock_t *lock)
{
    /* Atomically get a ticket and increment next_ticket */
    u32 my_ticket = __atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_RELAXED);

    /* Spin until our ticket is called */
    while (__atomic_load_n(&lock->now_serving, __ATOMIC_ACQUIRE) != my_ticket) {
        cpu_pause();
    }
}

/* Release spinlock */
static __always_inline void spin_unlock(spinlock_t *lock)
{
    /* Increment now_serving to let next waiter proceed */
    __atomic_fetch_add(&lock->now_serving, 1, __ATOMIC_RELEASE);
}

/* Try to acquire spinlock (non-blocking) */
static __always_inline bool spin_trylock(spinlock_t *lock)
{
    u32 ticket = __atomic_load_n(&lock->next_ticket, __ATOMIC_RELAXED);
    u32 serving = __atomic_load_n(&lock->now_serving, __ATOMIC_RELAXED);

    /* Lock is free if next_ticket == now_serving */
    if (ticket != serving) {
        return false;
    }

    /* Try to acquire the ticket */
    return __atomic_compare_exchange_n(&lock->next_ticket, &ticket, ticket + 1,
                                       false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

/* Check if lock is held (for debugging) */
static __always_inline bool spin_is_locked(spinlock_t *lock)
{
    return __atomic_load_n(&lock->next_ticket, __ATOMIC_RELAXED) !=
           __atomic_load_n(&lock->now_serving, __ATOMIC_RELAXED);
}

/*
 * IRQ-safe spinlock variants
 *
 * These save and restore the interrupt flag to prevent deadlock
 * when the same lock might be acquired from both interrupt and
 * non-interrupt context.
 */

static __always_inline void spin_lock_irqsave(spinlock_t *lock, u64 *flags)
{
    *flags = local_irq_save();
    spin_lock(lock);
}

static __always_inline void spin_unlock_irqrestore(spinlock_t *lock, u64 flags)
{
    spin_unlock(lock);
    local_irq_restore(flags);
}

/* Disable interrupts and acquire lock */
static __always_inline void spin_lock_irq(spinlock_t *lock)
{
    cli();
    spin_lock(lock);
}

/* Release lock and enable interrupts */
static __always_inline void spin_unlock_irq(spinlock_t *lock)
{
    spin_unlock(lock);
    sti();
}

/*
 * Reader-writer spinlock
 *
 * Allows multiple readers or a single writer.
 * Writers have priority to prevent starvation.
 */
typedef struct rwlock {
    volatile i32 count;     /* >0 = readers, -1 = writer, 0 = free */
    spinlock_t wait_lock;   /* Protects the wait queue */
} rwlock_t;

#define RWLOCK_INIT { .count = 0, .wait_lock = SPINLOCK_INIT }
#define DEFINE_RWLOCK(name) rwlock_t name = RWLOCK_INIT

static __always_inline void rwlock_init(rwlock_t *lock)
{
    lock->count = 0;
    spin_init(&lock->wait_lock);
}

/* Acquire read lock */
static __always_inline void read_lock(rwlock_t *lock)
{
    for (;;) {
        /* Wait until no writer */
        while (__atomic_load_n(&lock->count, __ATOMIC_RELAXED) < 0) {
            cpu_pause();
        }

        /* Try to increment reader count */
        i32 count = __atomic_load_n(&lock->count, __ATOMIC_RELAXED);
        if (count >= 0) {
            if (__atomic_compare_exchange_n(&lock->count, &count, count + 1,
                                            false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return;
            }
        }
    }
}

/* Release read lock */
static __always_inline void read_unlock(rwlock_t *lock)
{
    __atomic_fetch_sub(&lock->count, 1, __ATOMIC_RELEASE);
}

/* Acquire write lock */
static __always_inline void write_lock(rwlock_t *lock)
{
    for (;;) {
        /* Wait until free */
        while (__atomic_load_n(&lock->count, __ATOMIC_RELAXED) != 0) {
            cpu_pause();
        }

        /* Try to set writer flag */
        i32 expected = 0;
        if (__atomic_compare_exchange_n(&lock->count, &expected, -1,
                                        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

/* Release write lock */
static __always_inline void write_unlock(rwlock_t *lock)
{
    __atomic_store_n(&lock->count, 0, __ATOMIC_RELEASE);
}

/*
 * Atomic operations
 */
typedef struct atomic {
    volatile i32 value;
} atomic_t;

typedef struct atomic64 {
    volatile i64 value;
} atomic64_t;

typedef struct atomic_ulong {
    volatile u64 value;
} atomic_ulong_t;

#define ATOMIC_INIT(v) { .value = (v) }

static __always_inline i32 atomic_read(const atomic_t *v)
{
    return __atomic_load_n(&v->value, __ATOMIC_RELAXED);
}

static __always_inline void atomic_set(atomic_t *v, i32 i)
{
    __atomic_store_n(&v->value, i, __ATOMIC_RELAXED);
}

static __always_inline void atomic_add(atomic_t *v, i32 i)
{
    __atomic_fetch_add(&v->value, i, __ATOMIC_RELAXED);
}

static __always_inline void atomic_sub(atomic_t *v, i32 i)
{
    __atomic_fetch_sub(&v->value, i, __ATOMIC_RELAXED);
}

static __always_inline void atomic_inc(atomic_t *v)
{
    __atomic_fetch_add(&v->value, 1, __ATOMIC_RELAXED);
}

static __always_inline void atomic_dec(atomic_t *v)
{
    __atomic_fetch_sub(&v->value, 1, __ATOMIC_RELAXED);
}

static __always_inline i32 atomic_fetch_add(atomic_t *v, i32 i)
{
    return __atomic_fetch_add(&v->value, i, __ATOMIC_SEQ_CST);
}

static __always_inline i32 atomic_fetch_sub(atomic_t *v, i32 i)
{
    return __atomic_fetch_sub(&v->value, i, __ATOMIC_SEQ_CST);
}

static __always_inline bool atomic_dec_and_test(atomic_t *v)
{
    return __atomic_fetch_sub(&v->value, 1, __ATOMIC_SEQ_CST) == 1;
}

/* 64-bit atomics */
static __always_inline i64 atomic64_read(const atomic64_t *v)
{
    return __atomic_load_n(&v->value, __ATOMIC_RELAXED);
}

static __always_inline void atomic64_set(atomic64_t *v, i64 i)
{
    __atomic_store_n(&v->value, i, __ATOMIC_RELAXED);
}

static __always_inline i64 atomic64_fetch_add(atomic64_t *v, i64 i)
{
    return __atomic_fetch_add(&v->value, i, __ATOMIC_SEQ_CST);
}

/* Unsigned long atomics */
static __always_inline u64 atomic_ulong_read(const atomic_ulong_t *v)
{
    return __atomic_load_n(&v->value, __ATOMIC_RELAXED);
}

static __always_inline void atomic_ulong_set(atomic_ulong_t *v, u64 i)
{
    __atomic_store_n(&v->value, i, __ATOMIC_RELAXED);
}

static __always_inline u64 atomic_ulong_fetch_add(atomic_ulong_t *v, u64 i)
{
    return __atomic_fetch_add(&v->value, i, __ATOMIC_SEQ_CST);
}

static __always_inline u64 atomic_ulong_fetch_sub(atomic_ulong_t *v, u64 i)
{
    return __atomic_fetch_sub(&v->value, i, __ATOMIC_SEQ_CST);
}

static __always_inline u64 atomic_ulong_fetch_or(atomic_ulong_t *v, u64 i)
{
    return __atomic_fetch_or(&v->value, i, __ATOMIC_SEQ_CST);
}

static __always_inline u64 atomic_ulong_fetch_and(atomic_ulong_t *v, u64 i)
{
    return __atomic_fetch_and(&v->value, i, __ATOMIC_SEQ_CST);
}

#endif /* _OCEAN_SPINLOCK_H */
