/*
 * Ocean Kernel - Scheduler
 *
 * Priority-based preemptive scheduler with per-CPU run queues.
 */

#ifndef _OCEAN_SCHED_H
#define _OCEAN_SCHED_H

#include <ocean/types.h>
#include <ocean/list.h>
#include <ocean/spinlock.h>
#include <ocean/process.h>

/*
 * Per-CPU run queue
 *
 * Each CPU has its own run queue to reduce lock contention.
 * Threads are distributed across CPUs for load balancing.
 */
struct run_queue {
    spinlock_t lock;

    /* Number of runnable threads */
    u64 nr_running;

    /* Priority queues (one list per priority level) */
    struct list_head queue[MAX_PRIO];

    /* Bitmap of non-empty queues for fast lookup */
    u64 bitmap[3];  /* 140 bits = 3 x 64-bit words */

    /* Currently running thread on this CPU */
    struct thread *curr;

    /* Idle thread for this CPU */
    struct thread *idle;

    /* Statistics */
    u64 switches;           /* Context switches */
    u64 total_time;         /* Total running time */
    u64 idle_time;          /* Time spent idle */

    /* CPU identification */
    int cpu_id;

    /* Timer state */
    u64 tick_count;         /* Timer ticks */
    u64 last_tick;          /* Last tick timestamp */
};

/* Per-CPU run queues */
extern struct run_queue *runqueues;
extern int nr_cpus;

/* Get current CPU's run queue */
struct run_queue *this_rq(void);

/* Get run queue for specific CPU */
struct run_queue *cpu_rq(int cpu);

/*
 * Scheduler API
 */

/* Initialize scheduler */
void sched_init(void);

/* Initialize scheduler for an AP (application processor) */
void sched_init_ap(int cpu_id);

/* Main scheduler entry - pick next thread and switch */
void schedule(void);

/* Add thread to run queue */
void sched_add(struct thread *t);

/* Remove thread from run queue */
void sched_remove(struct thread *t);

/* Thread yields CPU voluntarily */
void sched_yield(void);

/* Called on timer tick */
void sched_tick(void);

/* Set thread priority */
void sched_set_priority(struct thread *t, int prio);

/* Set thread nice value */
void sched_set_nice(struct thread *t, int nice);

/* Wake up a thread */
void sched_wakeup(struct thread *t);

/*
 * Context switch
 */

/* Low-level context switch (in assembly) */
void switch_context(struct cpu_context *prev, struct cpu_context *next);

/* Switch to a new thread */
void switch_to(struct thread *prev, struct thread *next);

/*
 * Preemption control
 */

/* Disable preemption */
void preempt_disable(void);

/* Enable preemption */
void preempt_enable(void);

/* Check if preemption is disabled */
int preempt_count(void);

/*
 * Wait queues
 */
struct wait_queue {
    spinlock_t lock;
    struct list_head head;
};

#define DECLARE_WAIT_QUEUE(name) \
    struct wait_queue name = { \
        .lock = SPINLOCK_UNLOCKED, \
        .head = LIST_HEAD_INIT(name.head) \
    }

void wait_queue_init(struct wait_queue *wq);
void wait_event(struct wait_queue *wq);
void wake_up(struct wait_queue *wq);
void wake_up_all(struct wait_queue *wq);

/*
 * Timer and time management
 */

/* Timer frequency (ticks per second) */
#define HZ      100

/* Nanoseconds per tick */
#define TICK_NS (1000000000ULL / HZ)

/* Get current time in nanoseconds */
u64 get_time_ns(void);

/* Get current tick count */
u64 get_ticks(void);

/* Sleep for specified milliseconds */
void msleep(u64 ms);

/* Sleep for specified nanoseconds */
void nsleep(u64 ns);

/*
 * Load balancing (placeholder for SMP)
 */
void sched_balance(void);

/*
 * Debug
 */
void sched_dump_runqueue(int cpu);
void sched_dump_stats(void);

#endif /* _OCEAN_SCHED_H */
