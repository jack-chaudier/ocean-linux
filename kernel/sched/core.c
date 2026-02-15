/*
 * Ocean Kernel - Scheduler Core
 *
 * Priority-based preemptive scheduler with O(1) priority lookup.
 */

#include <ocean/sched.h>
#include <ocean/process.h>
#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/list.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Context switch (assembly) */
extern void switch_context(struct cpu_context *prev, struct cpu_context *next);

/* Per-CPU run queues */
struct run_queue *runqueues;
int nr_cpus = 1;  /* Start with BSP only */

/* Current thread pointer (per-CPU, but we start with one CPU) */
struct thread *current_thread = NULL;

/* Preemption count (per-CPU) */
static int _preempt_count = 0;

/* Global tick counter */
static u64 global_ticks = 0;

/* Time tracking (simple monotonic counter based on ticks) */
static u64 boot_time_ns = 0;

/*
 * Bitmap operations for fast priority queue lookup
 */
static inline void bitmap_set(u64 *bitmap, int bit)
{
    bitmap[bit / 64] |= (1ULL << (bit % 64));
}

static inline void bitmap_clear(u64 *bitmap, int bit)
{
    bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

static inline int bitmap_test(u64 *bitmap, int bit)
{
    return (bitmap[bit / 64] >> (bit % 64)) & 1;
}

/* Find first set bit (returns -1 if none) */
static inline int bitmap_ffs(u64 *bitmap, int nbits)
{
    int nwords = (nbits + 63) / 64;
    for (int i = 0; i < nwords; i++) {
        if (bitmap[i]) {
            return i * 64 + __builtin_ctzll(bitmap[i]);
        }
    }
    return -1;
}

/*
 * Get current CPU's run queue
 */
struct run_queue *this_rq(void)
{
    /* TODO: Use per-CPU data when SMP is implemented */
    return &runqueues[0];
}

/*
 * Get run queue for specific CPU
 */
struct run_queue *cpu_rq(int cpu)
{
    if (cpu < 0 || cpu >= nr_cpus) {
        return NULL;
    }
    return &runqueues[cpu];
}

/*
 * Preemption control
 */
void preempt_disable(void)
{
    _preempt_count++;
}

void preempt_enable(void)
{
    if (_preempt_count > 0) {
        _preempt_count--;
    }

    /* Check if we need to reschedule */
    if (_preempt_count == 0 && current_thread &&
        (current_thread->flags & TF_NEED_RESCHED)) {
        schedule();
    }
}

int preempt_count(void)
{
    return _preempt_count;
}

/*
 * Initialize a run queue
 */
static void rq_init(struct run_queue *rq, int cpu_id)
{
    spin_init(&rq->lock);
    rq->nr_running = 0;
    rq->cpu_id = cpu_id;
    rq->curr = NULL;
    rq->idle = NULL;
    rq->switches = 0;
    rq->total_time = 0;
    rq->idle_time = 0;
    rq->tick_count = 0;
    rq->last_tick = 0;

    /* Initialize priority queues */
    for (int i = 0; i < MAX_PRIO; i++) {
        INIT_LIST_HEAD(&rq->queue[i]);
    }

    /* Clear bitmap */
    memset(rq->bitmap, 0, sizeof(rq->bitmap));
}

/*
 * Add thread to run queue
 */
void sched_add(struct thread *t)
{
    struct run_queue *rq = this_rq();
    u64 flags;

    spin_lock_irqsave(&rq->lock, &flags);

    /* Add to appropriate priority queue */
    int prio = t->priority;
    if (prio < 0) prio = 0;
    if (prio >= MAX_PRIO) prio = MAX_PRIO - 1;

    list_add_tail(&t->run_list, &rq->queue[prio]);
    bitmap_set(rq->bitmap, prio);
    rq->nr_running++;

    t->state = TASK_RUNNING;
    t->cpu = rq->cpu_id;

    spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Remove thread from run queue
 */
void sched_remove(struct thread *t)
{
    struct run_queue *rq = cpu_rq(t->cpu);
    u64 flags;

    if (!rq) return;

    spin_lock_irqsave(&rq->lock, &flags);

    if (!list_empty(&t->run_list)) {
        list_del_init(&t->run_list);
        rq->nr_running--;

        /* Clear bitmap bit if queue is now empty */
        int prio = t->priority;
        if (list_empty(&rq->queue[prio])) {
            bitmap_clear(rq->bitmap, prio);
        }
    }

    spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Pick the next thread to run
 */
static struct thread *pick_next_thread(struct run_queue *rq)
{
    /* Find highest priority non-empty queue */
    int prio = bitmap_ffs(rq->bitmap, MAX_PRIO);

    if (prio < 0) {
        /* No runnable threads - return idle */
        return rq->idle;
    }

    /* Get first thread from this priority queue */
    struct thread *next = list_first_entry(&rq->queue[prio],
                                           struct thread, run_list);

    /* Remove from queue (will be re-added when it yields/blocks) */
    list_del_init(&next->run_list);
    rq->nr_running--;

    /* Clear bitmap if queue is now empty */
    if (list_empty(&rq->queue[prio])) {
        bitmap_clear(rq->bitmap, prio);
    }

    return next;
}

/*
 * Perform context switch
 */
void switch_to(struct thread *prev, struct thread *next)
{
    struct run_queue *rq = this_rq();

    /* Update run queue curr */
    rq->curr = next;
    current_thread = next;

    /* Switch address space if needed */
    if (prev->process != next->process) {
        if (next->process && next->process->mm) {
            /* Switch to new process's address space */
            extern void paging_switch(struct address_space *as);
            paging_switch(next->process->mm);
        }
    }

    /*
     * Update per-CPU kernel stack for syscalls.
     * When the next thread makes a syscall, it will use its own kernel stack.
     */
    if (next->kernel_stack) {
        extern void set_percpu_kernel_rsp(u64 rsp);
        u64 next_kstack_top = (u64)next->kernel_stack + next->kernel_stack_size - 8;
        set_percpu_kernel_rsp(next_kstack_top);
    }

    /* Update statistics */
    rq->switches++;
    next->last_run = global_ticks;

    /* Clear need_resched flag */
    next->flags &= ~TF_NEED_RESCHED;

    /* Perform the actual context switch */
    switch_context(&prev->context, &next->context);
}

/*
 * Main scheduler entry point
 */
void schedule(void)
{
    struct run_queue *rq = this_rq();
    struct thread *prev = current_thread;
    struct thread *next;
    u64 flags;

    if (!prev) {
        prev = rq->idle;
        current_thread = prev;
        rq->curr = prev;
    }

    preempt_disable();
    spin_lock_irqsave(&rq->lock, &flags);

    /* Put current thread back on run queue if it's still runnable */
    if (prev && prev->state == TASK_RUNNING && prev != rq->idle) {
        int prio = prev->priority;
        list_add_tail(&prev->run_list, &rq->queue[prio]);
        bitmap_set(rq->bitmap, prio);
        rq->nr_running++;
    }

    /* Pick next thread */
    next = pick_next_thread(rq);

    spin_unlock_irqrestore(&rq->lock, flags);

    /* Switch if different thread */
    if (next != prev) {
        switch_to(prev, next);
    }

    preempt_enable();
}

/*
 * Yield CPU voluntarily
 */
void sched_yield(void)
{
    current_thread->flags |= TF_NEED_RESCHED;
    schedule();
}

/*
 * Timer tick handler
 */
void sched_tick(void)
{
    struct run_queue *rq = this_rq();
    struct thread *curr = current_thread;

    rq->tick_count++;
    global_ticks++;

    if (!curr || curr == rq->idle) {
        rq->idle_time += TICK_NS;

        /* Check if there are runnable threads to switch to */
        if (rq->nr_running > 0) {
            /* Mark that we need to reschedule */
            if (curr) {
                curr->flags |= TF_NEED_RESCHED;
            }
        }
        return;
    }

    /* Update thread's time slice */
    if (curr->time_slice > TICK_NS) {
        curr->time_slice -= TICK_NS;
    } else {
        /* Time slice expired - need reschedule */
        curr->time_slice = DEFAULT_TIME_SLICE;
        curr->flags |= TF_NEED_RESCHED;
    }

    /* Update time accounting */
    curr->system_time += TICK_NS;  /* Assume kernel time for now */
    rq->total_time += TICK_NS;
}

/*
 * Wake up a thread
 */
void sched_wakeup(struct thread *t)
{
    if (t->state == TASK_RUNNING) {
        return;  /* Already runnable */
    }

    t->state = TASK_RUNNING;
    t->time_slice = DEFAULT_TIME_SLICE;
    sched_add(t);
}

/*
 * Set thread priority
 */
void sched_set_priority(struct thread *t, int prio)
{
    if (prio < 0) prio = 0;
    if (prio >= MAX_PRIO) prio = MAX_PRIO - 1;

    u64 flags;
    struct run_queue *rq = cpu_rq(t->cpu);

    if (rq) {
        spin_lock_irqsave(&rq->lock, &flags);
    }

    /* Remove from current queue if on one */
    if (!list_empty(&t->run_list)) {
        list_del_init(&t->run_list);
        if (list_empty(&rq->queue[t->priority])) {
            bitmap_clear(rq->bitmap, t->priority);
        }

        /* Add to new priority queue */
        t->priority = prio;
        list_add_tail(&t->run_list, &rq->queue[prio]);
        bitmap_set(rq->bitmap, prio);
    } else {
        t->priority = prio;
    }

    if (rq) {
        spin_unlock_irqrestore(&rq->lock, flags);
    }
}

/*
 * Set thread nice value
 */
void sched_set_nice(struct thread *t, int nice)
{
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;

    t->nice = nice;
    sched_set_priority(t, NICE_TO_PRIO(nice));
}

/*
 * Time functions
 */
u64 get_ticks(void)
{
    return global_ticks;
}

u64 get_time_ns(void)
{
    return boot_time_ns + (global_ticks * TICK_NS);
}

void msleep(u64 ms)
{
    u64 end = global_ticks + (ms * HZ / 1000);
    while (global_ticks < end) {
        sched_yield();
    }
}

void nsleep(u64 ns)
{
    u64 ticks = (ns + TICK_NS - 1) / TICK_NS;
    u64 end = global_ticks + ticks;
    while (global_ticks < end) {
        sched_yield();
    }
}

/*
 * Thread sleep/wakeup on a channel
 *
 * A channel is any pointer that identifies what we're waiting for.
 * Used for simple blocking without a full wait queue structure.
 */
void thread_sleep(void *channel)
{
    struct thread *t = current_thread;

    if (!t) {
        return;
    }

    /* Mark as sleeping on this channel */
    t->wait_channel = channel;
    t->state = TASK_INTERRUPTIBLE;

    /* Remove from run queue and reschedule */
    sched_remove(t);
    schedule();

    /* Woken up - clear wait channel */
    t->wait_channel = NULL;
}

void thread_wakeup(void *channel)
{
    /* Wake all threads sleeping on this channel. */
    extern struct list_head all_threads;
    extern spinlock_t thread_list_lock;

    u64 flags;
    spin_lock_irqsave(&thread_list_lock, &flags);

    struct thread *t;
    list_for_each_entry(t, &all_threads, all_list) {
        if (t->wait_channel == channel &&
            (t->state == TASK_INTERRUPTIBLE ||
             t->state == TASK_UNINTERRUPTIBLE)) {
            sched_wakeup(t);
        }
    }

    spin_unlock_irqrestore(&thread_list_lock, flags);
}

/*
 * Wait queue implementation
 */
void wait_queue_init(struct wait_queue *wq)
{
    spin_init(&wq->lock);
    INIT_LIST_HEAD(&wq->head);
}

void wait_event(struct wait_queue *wq)
{
    struct thread *t = current_thread;
    u64 flags;

    spin_lock_irqsave(&wq->lock, &flags);

    /* Add to wait queue */
    list_add_tail(&t->wait_list, &wq->head);
    t->state = TASK_INTERRUPTIBLE;

    spin_unlock_irqrestore(&wq->lock, flags);

    /* Remove from run queue and reschedule */
    sched_remove(t);
    schedule();

    /* We've been woken up - remove from wait queue */
    spin_lock_irqsave(&wq->lock, &flags);
    list_del_init(&t->wait_list);
    spin_unlock_irqrestore(&wq->lock, flags);
}

void wake_up(struct wait_queue *wq)
{
    u64 flags;

    spin_lock_irqsave(&wq->lock, &flags);

    if (!list_empty(&wq->head)) {
        struct thread *t = list_first_entry(&wq->head, struct thread, wait_list);
        list_del_init(&t->wait_list);
        sched_wakeup(t);
    }

    spin_unlock_irqrestore(&wq->lock, flags);
}

void wake_up_all(struct wait_queue *wq)
{
    u64 flags;

    spin_lock_irqsave(&wq->lock, &flags);

    while (!list_empty(&wq->head)) {
        struct thread *t = list_first_entry(&wq->head, struct thread, wait_list);
        list_del_init(&t->wait_list);
        sched_wakeup(t);
    }

    spin_unlock_irqrestore(&wq->lock, flags);
}

/*
 * Debug functions
 */
void sched_dump_runqueue(int cpu)
{
    struct run_queue *rq = cpu_rq(cpu);
    if (!rq) return;

    kprintf("Run Queue CPU %d:\n", cpu);
    kprintf("  Running: %llu threads\n", rq->nr_running);
    const char *curr_name = "(none)";
    if (rq->curr) {
        if (rq->curr->process) {
            curr_name = rq->curr->process->name;
        } else if (rq->curr->flags & TF_IDLE) {
            curr_name = "[idle]";
        } else {
            curr_name = "[kernel]";
        }
    }
    kprintf("  Current: %s (tid %d)\n", curr_name, rq->curr ? rq->curr->tid : -1);
    kprintf("  Switches: %llu\n", rq->switches);
    kprintf("  Ticks: %llu\n", rq->tick_count);
}

void sched_dump_stats(void)
{
    kprintf("\nScheduler Statistics:\n");
    kprintf("  Total ticks: %llu\n", global_ticks);
    kprintf("  CPUs: %d\n", nr_cpus);

    for (int i = 0; i < nr_cpus; i++) {
        sched_dump_runqueue(i);
    }
}

/*
 * Create idle thread for a CPU
 */
static struct thread *create_idle_thread(int cpu_id)
{
    struct thread *idle = kmalloc(sizeof(struct thread));
    if (!idle) return NULL;

    memset(idle, 0, sizeof(*idle));

    idle->tid = -1 - cpu_id;  /* Negative TIDs for idle threads */
    idle->pid = 0;
    idle->process = NULL;
    idle->state = TASK_RUNNING;
    idle->flags = TF_KTHREAD | TF_IDLE;
    idle->priority = MAX_PRIO - 1;  /* Lowest priority */
    idle->cpu = cpu_id;
    idle->cpu_mask = 1ULL << cpu_id;

    INIT_LIST_HEAD(&idle->run_list);
    INIT_LIST_HEAD(&idle->thread_list);
    INIT_LIST_HEAD(&idle->wait_list);

    return idle;
}

/*
 * Initialize scheduler for BSP
 */
void sched_init(void)
{
    kprintf("Initializing scheduler...\n");

    /* Allocate run queues (just one for now) */
    runqueues = kmalloc(sizeof(struct run_queue) * 1);
    if (!runqueues) {
        kprintf("Failed to allocate run queues!\n");
        return;
    }

    /* Initialize BSP run queue */
    rq_init(&runqueues[0], 0);

    /* Create idle thread */
    struct thread *idle = create_idle_thread(0);
    if (!idle) {
        kprintf("Failed to create idle thread!\n");
        return;
    }
    runqueues[0].idle = idle;
    runqueues[0].curr = idle;
    current_thread = idle;

    kprintf("  Run queue initialized for CPU 0\n");
    kprintf("  Idle thread created (tid %d)\n", idle->tid);
    kprintf("Scheduler initialized\n");
}

/*
 * Initialize scheduler for an AP
 */
void sched_init_ap(int cpu_id)
{
    /* TODO: Initialize run queue for AP when SMP is implemented */
    (void)cpu_id;
}
