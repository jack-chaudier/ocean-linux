/*
 * Ocean Kernel - Process Management
 *
 * Process creation, destruction, fork, and related operations.
 */

#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/vmm.h>
#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/list.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Assembly entry point for new forked processes */
extern void ret_from_fork(void);

/* Global process list */
static LIST_HEAD(process_list);
static spinlock_t process_list_lock;

/* Global thread list for channel-based wakeups */
struct list_head all_threads = LIST_HEAD_INIT(all_threads);
spinlock_t thread_list_lock;

/* PID allocation bitmap */
static u64 pid_bitmap[PID_MAX / 64];
static spinlock_t pid_lock;
static pid_t next_pid = 1;

/* Init process (PID 1) */
struct process *init_process = NULL;

/* Forward declarations */
static void free_kernel_stack(void *stack);

static void thread_global_add(struct thread *t)
{
    u64 flags;
    spin_lock_irqsave(&thread_list_lock, &flags);
    list_add_tail(&t->all_list, &all_threads);
    spin_unlock_irqrestore(&thread_list_lock, flags);
}

static void thread_global_remove(struct thread *t)
{
    u64 flags;
    spin_lock_irqsave(&thread_list_lock, &flags);
    if (!list_empty(&t->all_list)) {
        list_del_init(&t->all_list);
    }
    spin_unlock_irqrestore(&thread_list_lock, flags);
}

static void process_reap(struct process *child)
{
    if (!child) {
        return;
    }

    if (child->main_thread) {
        thread_global_remove(child->main_thread);
        free_kernel_stack(child->main_thread->kernel_stack);
        kfree(child->main_thread);
        child->main_thread = NULL;
    }

    if (child->mm) {
        vmm_destroy_address_space(child->mm);
        child->mm = NULL;
    }

    u64 flags;
    spin_lock_irqsave(&process_list_lock, &flags);
    if (!list_empty(&child->proc_list)) {
        list_del_init(&child->proc_list);
    }
    spin_unlock_irqrestore(&process_list_lock, flags);

    free_pid(child->pid);
    kfree(child);
}

/*
 * Allocate a new PID
 */
pid_t alloc_pid(void)
{
    u64 flags;
    spin_lock_irqsave(&pid_lock, &flags);

    pid_t pid = -1;

    /* Search for free PID starting from next_pid */
    for (pid_t i = next_pid; i < PID_MAX; i++) {
        int word = i / 64;
        int bit = i % 64;

        if (!(pid_bitmap[word] & (1ULL << bit))) {
            pid_bitmap[word] |= (1ULL << bit);
            pid = i;
            next_pid = i + 1;
            if (next_pid >= PID_MAX) {
                next_pid = 1;
            }
            break;
        }
    }

    /* Wrap around if not found */
    if (pid < 0) {
        for (pid_t i = 1; i < next_pid; i++) {
            int word = i / 64;
            int bit = i % 64;

            if (!(pid_bitmap[word] & (1ULL << bit))) {
                pid_bitmap[word] |= (1ULL << bit);
                pid = i;
                next_pid = i + 1;
                break;
            }
        }
    }

    spin_unlock_irqrestore(&pid_lock, flags);
    return pid;
}

/*
 * Free a PID
 */
void free_pid(pid_t pid)
{
    if (pid <= 0 || pid >= PID_MAX) {
        return;
    }

    u64 flags;
    spin_lock_irqsave(&pid_lock, &flags);

    int word = pid / 64;
    int bit = pid % 64;
    pid_bitmap[word] &= ~(1ULL << bit);

    spin_unlock_irqrestore(&pid_lock, flags);
}

/*
 * Allocate a kernel stack for a thread
 */
static void *alloc_kernel_stack(void)
{
    /* Allocate 2 pages for kernel stack (8KB) */
    void *stack = kmalloc(KERNEL_STACK_SIZE);
    if (stack) {
        memset(stack, 0, KERNEL_STACK_SIZE);
    }
    return stack;
}

/*
 * Free a kernel stack
 */
static void free_kernel_stack(void *stack)
{
    if (stack) {
        kfree(stack);
    }
}

/*
 * Initialize process subsystem
 */
void process_init(void)
{
    kprintf("Initializing process subsystem...\n");

    spin_init(&process_list_lock);
    spin_init(&thread_list_lock);
    spin_init(&pid_lock);
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    INIT_LIST_HEAD(&all_threads);

    /* Reserve PID 0 for kernel/idle */
    pid_bitmap[0] |= 1;

    kprintf("Process subsystem initialized\n");
}

/*
 * Create a new process
 */
struct process *process_create(const char *name)
{
    struct process *proc = kmalloc(sizeof(struct process));
    if (!proc) {
        kprintf("process_create: failed to allocate process\n");
        return NULL;
    }

    memset(proc, 0, sizeof(*proc));

    /* Allocate PID */
    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        kprintf("process_create: out of PIDs\n");
        kfree(proc);
        return NULL;
    }

    /* Set name */
    if (name) {
        size_t len = strlen(name);
        if (len >= sizeof(proc->name)) {
            len = sizeof(proc->name) - 1;
        }
        memcpy(proc->name, name, len);
        proc->name[len] = '\0';
    }

    /* Initialize thread list */
    INIT_LIST_HEAD(&proc->threads);
    proc->nr_threads = 0;

    /* Initialize children list */
    INIT_LIST_HEAD(&proc->children);
    INIT_LIST_HEAD(&proc->sibling);

    /* Initialize process lock */
    spin_init(&proc->lock);

    /* Add to global process list */
    u64 flags;
    spin_lock_irqsave(&process_list_lock, &flags);
    list_add_tail(&proc->proc_list, &process_list);
    spin_unlock_irqrestore(&process_list_lock, flags);

    return proc;
}

/* External assembly function to enter user mode */
extern void enter_usermode(u64 rip, u64 rsp, u64 rflags);

/*
 * User thread start trampoline
 *
 * This is the initial RIP for user threads. When the scheduler first
 * switches to this thread, it will execute this function which then
 * enters user mode properly via IRETQ.
 *
 * The user entry point and stack are passed in callee-saved registers:
 *   r12 = user RIP (entry point)
 *   r13 = user RSP (stack pointer)
 */
static void user_thread_start(void)
{
    struct thread *t = current_thread;

    /* Enter user mode - this never returns */
    enter_usermode(t->context.r12, t->context.r13, 0);

    /* Should never reach here */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * Kernel thread trampoline.
 *
 * The target function/argument are stored in r12/r13 in the saved context.
 */
static void kthread_entry(void)
{
    struct thread *t = current_thread;
    int (*fn)(void *) = (int (*)(void *))t->context.r12;
    void *arg = (void *)t->context.r13;

    int rc = 0;
    if (fn) {
        rc = fn(arg);
    }

    thread_exit(rc);
}

/*
 * Create the main thread for a process
 */
struct thread *process_create_main_thread(struct process *proc, u64 entry, u64 stack_top)
{
    struct thread *t = kmalloc(sizeof(struct thread));
    if (!t) {
        kprintf("process_create_main_thread: failed to allocate thread\n");
        return NULL;
    }

    memset(t, 0, sizeof(*t));

    /* Thread IDs: main thread has tid == pid */
    t->tid = proc->pid;
    t->pid = proc->pid;
    t->process = proc;

    /* Scheduling state */
    t->state = TASK_RUNNING;
    t->flags = 0;
    t->priority = DEFAULT_PRIO;
    t->static_prio = DEFAULT_PRIO;
    t->nice = 0;
    t->time_slice = DEFAULT_TIME_SLICE;

    /* Allocate kernel stack */
    t->kernel_stack = alloc_kernel_stack();
    if (!t->kernel_stack) {
        kprintf("process_create_main_thread: failed to allocate kernel stack\n");
        kfree(t);
        return NULL;
    }
    t->kernel_stack_size = KERNEL_STACK_SIZE;

    /* Set up initial context */
    u64 kstack_top = (u64)t->kernel_stack + t->kernel_stack_size;

    /* Initialize CPU context for first switch */
    memset(&t->context, 0, sizeof(t->context));
    t->context.rsp = kstack_top - 8;  /* Leave room for return address */
    t->context.rbp = 0;

    /*
     * For user threads: set up to enter user_thread_start trampoline,
     * which will then call enter_usermode with the real entry point.
     * We store the user entry and stack in callee-saved registers.
     */
    t->context.rip = (u64)user_thread_start;
    t->context.r12 = entry;      /* User entry point */
    t->context.r13 = stack_top;  /* User stack */

    /* User stack pointer (if user process) */
    t->user_stack = (void *)stack_top;

    /* Initialize list heads */
    INIT_LIST_HEAD(&t->run_list);
    INIT_LIST_HEAD(&t->thread_list);
    INIT_LIST_HEAD(&t->wait_list);
    INIT_LIST_HEAD(&t->all_list);

    /* CPU affinity - allow all CPUs */
    t->cpu = 0;
    t->cpu_mask = ~0ULL;

    /* Timing */
    t->start_time = get_ticks();

    /* Link to process */
    u64 flags;
    spin_lock_irqsave(&proc->lock, &flags);
    list_add_tail(&t->thread_list, &proc->threads);
    proc->nr_threads++;
    proc->main_thread = t;
    spin_unlock_irqrestore(&proc->lock, flags);

    thread_global_add(t);

    return t;
}

/*
 * Create a kernel thread
 */
struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name)
{
    /* Create a kernel process to hold this thread */
    struct process *proc = process_create(name);
    if (!proc) {
        return NULL;
    }

    /* Kernel processes have no address space */
    proc->mm = NULL;

    /* Create the thread */
    struct thread *t = kmalloc(sizeof(struct thread));
    if (!t) {
        u64 flags;
        spin_lock_irqsave(&process_list_lock, &flags);
        list_del_init(&proc->proc_list);
        spin_unlock_irqrestore(&process_list_lock, flags);
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }

    memset(t, 0, sizeof(*t));

    t->tid = proc->pid;
    t->pid = proc->pid;
    t->process = proc;

    t->state = TASK_RUNNING;
    t->flags = TF_KTHREAD;
    t->priority = DEFAULT_PRIO;
    t->static_prio = DEFAULT_PRIO;
    t->nice = 0;
    t->time_slice = DEFAULT_TIME_SLICE;

    /* Allocate kernel stack */
    t->kernel_stack = alloc_kernel_stack();
    if (!t->kernel_stack) {
        kfree(t);
        u64 flags;
        spin_lock_irqsave(&process_list_lock, &flags);
        list_del_init(&proc->proc_list);
        spin_unlock_irqrestore(&process_list_lock, flags);
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    t->kernel_stack_size = KERNEL_STACK_SIZE;

    /* Set up initial context to call fn(arg) */
    u64 kstack_top = (u64)t->kernel_stack + t->kernel_stack_size;

    /*
     * Set up stack so when we switch to this thread:
     * - RSP points to a location with kthread_entry on top
     * - RIP is set to kthread_entry
     * - RDI will be the function, RSI will be arg
     *
     * For simplicity, store fn and arg in registers that will be
     * loaded by switch_context.
     */
    t->context.rsp = kstack_top - 8;
    t->context.rip = (u64)kthread_entry;
    t->context.rbp = 0;
    t->context.r12 = (u64)fn;
    t->context.r13 = (u64)arg;

    INIT_LIST_HEAD(&t->run_list);
    INIT_LIST_HEAD(&t->thread_list);
    INIT_LIST_HEAD(&t->wait_list);
    INIT_LIST_HEAD(&t->all_list);

    t->cpu = 0;
    t->cpu_mask = ~0ULL;
    t->start_time = get_ticks();

    /* Link to process */
    u64 flags;
    spin_lock_irqsave(&proc->lock, &flags);
    list_add_tail(&t->thread_list, &proc->threads);
    proc->nr_threads++;
    proc->main_thread = t;
    spin_unlock_irqrestore(&proc->lock, flags);

    thread_global_add(t);

    return t;
}

/*
 * Start a thread (add to scheduler)
 */
void thread_start(struct thread *t)
{
    if (!t) return;
    sched_add(t);
}

/*
 * Find a process by PID
 */
struct process *process_find(pid_t pid)
{
    struct process *proc;
    u64 flags;

    spin_lock_irqsave(&process_list_lock, &flags);

    list_for_each_entry(proc, &process_list, proc_list) {
        if (proc->pid == pid) {
            spin_unlock_irqrestore(&process_list_lock, flags);
            return proc;
        }
    }

    spin_unlock_irqrestore(&process_list_lock, flags);
    return NULL;
}

/*
 * Find a thread by TID
 */
struct thread *thread_find(tid_t tid)
{
    struct process *proc;
    struct thread *t;
    u64 flags;

    spin_lock_irqsave(&process_list_lock, &flags);

    list_for_each_entry(proc, &process_list, proc_list) {
        u64 pflags;
        spin_lock_irqsave(&proc->lock, &pflags);

        list_for_each_entry(t, &proc->threads, thread_list) {
            if (t->tid == tid) {
                spin_unlock_irqrestore(&proc->lock, pflags);
                spin_unlock_irqrestore(&process_list_lock, flags);
                return t;
            }
        }

        spin_unlock_irqrestore(&proc->lock, pflags);
    }

    spin_unlock_irqrestore(&process_list_lock, flags);
    return NULL;
}

/*
 * Thread yields CPU
 */
void thread_yield(void)
{
    sched_yield();
}

/*
 * Thread exits
 */
void thread_exit(int code)
{
    struct thread *t = current_thread;
    struct process *proc = t->process;

    /* Mark thread as exiting */
    t->flags |= TF_EXITING;

    /* Remove from process */
    u64 flags;
    spin_lock_irqsave(&proc->lock, &flags);
    list_del_init(&t->thread_list);
    proc->nr_threads--;
    spin_unlock_irqrestore(&proc->lock, flags);

    thread_global_remove(t);

    /* If last thread, process exits too - become zombie and wake parent */
    if (proc->nr_threads == 0) {
        proc->exit_code = code;
        t->state = TASK_ZOMBIE;  /* Become zombie for wait() */

        /* Wake up parent if it's waiting */
        struct process *parent = proc->parent;
        if (parent) {
            thread_wakeup(parent);
        }
    } else {
        t->state = TASK_DEAD;
    }

    /* Schedule away - we'll never return */
    schedule();

    /* Should never reach here */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/*
 * Process exits
 */
void process_exit(int code)
{
    struct process *proc = get_current_process();
    if (!proc) {
        /* No process - just halt forever */
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    proc->exit_code = code;

    /*
     * Reparent children to init so someone can always reap them.
     */
    if (init_process && init_process != proc) {
        u64 flags, init_flags;
        spin_lock_irqsave(&proc->lock, &flags);
        while (!list_empty(&proc->children)) {
            struct process *child = list_first_entry(&proc->children,
                                                     struct process, sibling);
            list_del_init(&child->sibling);
            child->parent = init_process;

            spin_lock_irqsave(&init_process->lock, &init_flags);
            list_add_tail(&child->sibling, &init_process->children);
            spin_unlock_irqrestore(&init_process->lock, init_flags);
        }
        spin_unlock_irqrestore(&proc->lock, flags);
    }

    /* TODO:
     * - Terminate all threads
     * - Close all file descriptors
     * - Release address space
     * - Reparent children to init
     * - Notify parent
     * - Become zombie
     */

    thread_exit(code);
}

/*
 * Fork current process
 */
pid_t process_fork(void)
{
    struct thread *parent_thread = current_thread;
    struct process *parent = parent_thread->process;

    /* Create child process */
    struct process *child = process_create(parent->name);
    if (!child) {
        return -1;
    }

    /* Copy credentials */
    child->ppid = parent->pid;
    child->uid = parent->uid;
    child->euid = parent->euid;
    child->suid = parent->suid;
    child->gid = parent->gid;
    child->egid = parent->egid;
    child->sgid = parent->sgid;
    child->pgid = parent->pgid;
    child->sid = parent->sid;

    /* Set parent/child relationship */
    child->parent = parent;
    u64 flags;
    spin_lock_irqsave(&parent->lock, &flags);
    list_add_tail(&child->sibling, &parent->children);
    spin_unlock_irqrestore(&parent->lock, flags);

    /* Clone address space (COW) */
    if (parent->mm) {
        child->mm = vmm_clone_address_space(parent->mm);
        if (!child->mm) {
            spin_lock_irqsave(&parent->lock, &flags);
            if (!list_empty(&child->sibling)) {
                list_del_init(&child->sibling);
            }
            spin_unlock_irqrestore(&parent->lock, flags);
            process_reap(child);
            return -1;
        }
    }

    /* Create child's main thread as copy of parent thread */
    struct thread *child_thread = kmalloc(sizeof(struct thread));
    if (!child_thread) {
        spin_lock_irqsave(&parent->lock, &flags);
        if (!list_empty(&child->sibling)) {
            list_del_init(&child->sibling);
        }
        spin_unlock_irqrestore(&parent->lock, flags);
        process_reap(child);
        return -1;
    }

    /* Copy thread state */
    memcpy(child_thread, parent_thread, sizeof(*child_thread));

    /* Allocate new kernel stack for child */
    child_thread->kernel_stack = alloc_kernel_stack();
    if (!child_thread->kernel_stack) {
        kfree(child_thread);
        spin_lock_irqsave(&parent->lock, &flags);
        if (!list_empty(&child->sibling)) {
            list_del_init(&child->sibling);
        }
        spin_unlock_irqrestore(&parent->lock, flags);
        process_reap(child);
        return -1;
    }

    /* Copy kernel stack contents */
    memcpy(child_thread->kernel_stack, parent_thread->kernel_stack,
           parent_thread->kernel_stack_size);

    /* Update child thread fields */
    child_thread->tid = child->pid;
    child_thread->pid = child->pid;
    child_thread->process = child;
    child_thread->flags = parent_thread->flags | TF_FORKING;
    child_thread->time_slice = DEFAULT_TIME_SLICE;
    child_thread->start_time = get_ticks();

    /* Reset timing stats */
    child_thread->user_time = 0;
    child_thread->system_time = 0;

    /* Initialize list heads (don't copy parent's links) */
    INIT_LIST_HEAD(&child_thread->run_list);
    INIT_LIST_HEAD(&child_thread->thread_list);
    INIT_LIST_HEAD(&child_thread->wait_list);
    INIT_LIST_HEAD(&child_thread->all_list);

    /* Set up context so child returns from fork with 0 */
    /* The context's RIP should point to ret_from_fork */
    child_thread->context.rip = (u64)ret_from_fork;

    /* Calculate child's kernel stack top */
    u64 child_kstack_top = (u64)child_thread->kernel_stack + child_thread->kernel_stack_size;

    /*
     * IMPORTANT: The syscall frame is on the per-CPU syscall stack, not the
     * thread's kernel stack. We need to copy it to the child's kernel stack
     * so ret_from_fork can find it.
     *
     * The syscall frame is 176 bytes (15 regs + error + int_no + 5 iret values).
     * get_percpu_kernel_rsp() returns (stack_top - 8), so after pushing 176 bytes,
     * the frame starts at (stack_top - 8 - 176) = (stack_top - 184).
     *
     * We copy to child_kstack_top - 176 so that ret_from_fork's
     * `lea rsp, [r12 - 176]` with r12=child_kstack_top finds r15 correctly.
     */
    extern u64 get_percpu_kernel_rsp(void);
    u64 percpu_stack_rsp = get_percpu_kernel_rsp();  /* = stack_top - 8 */

    /* Source: where the frame actually is on per-CPU stack */
    u64 *src = (u64 *)(percpu_stack_rsp - 176);  /* = stack_top - 184 */
    /* Destination: where ret_from_fork expects it */
    u64 *dst = (u64 *)(child_kstack_top - 176);
    for (int i = 0; i < 176 / 8; i++) {
        dst[i] = src[i];
    }

    /* r12 passes the kernel stack top to ret_from_fork */
    child_thread->context.r12 = child_kstack_top;

    /* RSP doesn't matter much since ret_from_fork will reset it based on r12 */
    child_thread->context.rsp = child_kstack_top - 256;

    /* Clear other callee-saved registers */
    child_thread->context.rbp = 0;
    child_thread->context.rbx = 0;
    child_thread->context.r13 = 0;
    child_thread->context.r14 = 0;
    child_thread->context.r15 = 0;

    /* Link child thread to child process */
    spin_lock_irqsave(&child->lock, &flags);
    list_add_tail(&child_thread->thread_list, &child->threads);
    child->nr_threads++;
    child->main_thread = child_thread;
    spin_unlock_irqrestore(&child->lock, flags);

    thread_global_add(child_thread);

    /* Add child thread to scheduler */
    child_thread->flags &= ~TF_FORKING;
    sched_add(child_thread);

    /* Parent returns child's PID */
    return child->pid;
}

/*
 * Wait for child process
 */
pid_t process_wait(int *status)
{
    struct process *proc = get_current_process();
    if (!proc || !current_thread) {
        return -1;
    }

retry:
    {
        u64 flags;
        spin_lock_irqsave(&proc->lock, &flags);

        /* First check if we have any children at all */
        if (list_empty(&proc->children)) {
            spin_unlock_irqrestore(&proc->lock, flags);
            return -1;  /* No children - ECHILD */
        }

        /* Check for zombie children */
        struct process *child;
        list_for_each_entry(child, &proc->children, sibling) {
            if (child->main_thread &&
                child->main_thread->state == TASK_ZOMBIE) {
                pid_t pid = child->pid;
                if (status) {
                    *status = child->exit_code;
                }

                /* Remove from children list */
                list_del_init(&child->sibling);

                spin_unlock_irqrestore(&proc->lock, flags);

                process_reap(child);

                return pid;
            }
        }

        spin_unlock_irqrestore(&proc->lock, flags);
    }

    /* No zombie children - block until a child exits. */
    thread_sleep(proc);

    /* We were woken up - check again for zombies */
    goto retry;
}

/*
 * Kill a process
 */
int process_kill(pid_t pid, int sig)
{
    struct process *proc = process_find(pid);
    if (!proc) {
        return -1;
    }

    /* TODO: Implement proper signal delivery */
    (void)sig;

    /* For now, just mark for exit */
    if (proc->main_thread) {
        proc->main_thread->flags |= TF_EXITING;
    }

    return 0;
}

/*
 * Dump process list (debug)
 */
void process_dump(void)
{
    kprintf("\nProcess List:\n");
    kprintf("  PID  PPID  NAME            THREADS  STATE\n");
    kprintf("  ---  ----  --------------  -------  -----\n");

    struct process *proc;
    u64 flags;

    spin_lock_irqsave(&process_list_lock, &flags);

    list_for_each_entry(proc, &process_list, proc_list) {
        const char *state = "?";
        if (proc->main_thread) {
            switch (proc->main_thread->state) {
                case TASK_RUNNING: state = "RUN"; break;
                case TASK_INTERRUPTIBLE: state = "SLP"; break;
                case TASK_UNINTERRUPTIBLE: state = "DIS"; break;
                case TASK_STOPPED: state = "STP"; break;
                case TASK_ZOMBIE: state = "ZOM"; break;
                case TASK_DEAD: state = "DEA"; break;
            }
        }

        kprintf("  %3d  %4d  %-14s  %7d  %s\n",
                proc->pid, proc->ppid, proc->name,
                proc->nr_threads, state);
    }

    spin_unlock_irqrestore(&process_list_lock, flags);
}
