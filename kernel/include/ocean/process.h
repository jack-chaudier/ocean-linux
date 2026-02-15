/*
 * Ocean Kernel - Process and Thread Management
 *
 * Defines process and thread structures, states, and management APIs.
 */

#ifndef _OCEAN_PROCESS_H
#define _OCEAN_PROCESS_H

#include <ocean/types.h>
#include <ocean/list.h>
#include <ocean/spinlock.h>

/* Forward declarations */
struct address_space;
struct vm_area;

/*
 * Process/Thread States
 */
enum task_state {
    TASK_RUNNING = 0,       /* Currently running or ready to run */
    TASK_INTERRUPTIBLE,     /* Sleeping, can be woken by signals */
    TASK_UNINTERRUPTIBLE,   /* Sleeping, cannot be interrupted */
    TASK_STOPPED,           /* Stopped (e.g., by SIGSTOP) */
    TASK_ZOMBIE,            /* Terminated, waiting for parent to reap */
    TASK_DEAD,              /* Being removed */
};

/*
 * Task flags
 */
#define TF_KTHREAD      (1 << 0)    /* Kernel thread (no user space) */
#define TF_IDLE         (1 << 1)    /* Idle thread */
#define TF_NEED_RESCHED (1 << 2)    /* Needs rescheduling */
#define TF_EXITING      (1 << 3)    /* Thread is exiting */
#define TF_FORKING      (1 << 4)    /* In middle of fork */

/*
 * CPU context saved during context switch
 * These are the callee-saved registers on x86_64
 */
struct cpu_context {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 rbx;
    u64 rbp;
    u64 rsp;
    u64 rip;        /* Return address */
};

/*
 * Full register state (saved on interrupt/syscall)
 */
struct pt_regs {
    /* Pushed by us */
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rbp;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;

    /* Pushed by ISR stub */
    u64 int_no;
    u64 error_code;

    /* Pushed by CPU */
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

/*
 * Thread structure (kernel and user threads)
 *
 * Each thread has its own stack and CPU context.
 * Multiple threads can share a process (address space).
 */
struct thread {
    /* Thread identification */
    tid_t tid;                      /* Thread ID */
    pid_t pid;                      /* Parent process ID */
    struct process *process;        /* Owning process */

    /* Scheduling state */
    enum task_state state;          /* Current state */
    u32 flags;                      /* TF_* flags */
    int priority;                   /* Scheduling priority (0-139) */
    int static_prio;                /* Base priority */
    int nice;                       /* Nice value (-20 to 19) */
    u64 time_slice;                 /* Remaining time slice (ns) */

    /* CPU context */
    struct cpu_context context;     /* Saved context for switch */
    struct pt_regs *regs;           /* User registers (on kernel stack) */

    /* Stack */
    void *kernel_stack;             /* Kernel stack base */
    u64 kernel_stack_size;          /* Kernel stack size */
    void *user_stack;               /* User stack (in address space) */

    /* Timing */
    u64 start_time;                 /* Thread creation time */
    u64 user_time;                  /* Time in user mode (ns) */
    u64 system_time;                /* Time in kernel mode (ns) */
    u64 last_run;                   /* Last time scheduled */

    /* Scheduler linkage */
    struct list_head run_list;      /* Link in run queue */
    struct list_head thread_list;   /* Link in process's thread list */
    struct list_head all_list;      /* Link in global thread list */

    /* Wait queue */
    struct list_head wait_list;     /* Link in wait queue */
    void *wait_channel;             /* What we're waiting for */
    int wait_result;                /* Result of wait */

    /* CPU affinity */
    int cpu;                        /* Current/last CPU */
    u64 cpu_mask;                   /* Allowed CPUs (bitmask) */
};

/*
 * Process structure
 *
 * A process is a collection of threads sharing an address space.
 * The main thread has tid == pid.
 */
struct process {
    /* Process identification */
    pid_t pid;                      /* Process ID */
    pid_t ppid;                     /* Parent process ID */
    pid_t pgid;                     /* Process group ID */
    pid_t sid;                      /* Session ID */

    /* Credentials */
    uid_t uid, euid, suid;          /* User IDs */
    gid_t gid, egid, sgid;          /* Group IDs */

    /* Memory */
    struct address_space *mm;       /* Address space (NULL for kernel) */

    /* Threads */
    struct list_head threads;       /* List of threads */
    int nr_threads;                 /* Number of threads */
    struct thread *main_thread;     /* Main thread (tid == pid) */

    /* Process tree */
    struct process *parent;         /* Parent process */
    struct list_head children;      /* Child processes */
    struct list_head sibling;       /* Link in parent's children list */

    /* Exit status */
    int exit_code;                  /* Exit code */
    int exit_signal;                /* Signal that caused exit */

    /* File descriptors (placeholder for future) */
    void *files;                    /* File descriptor table */

    /* Process name */
    char name[16];                  /* Process name (comm) */

    /* Synchronization */
    spinlock_t lock;

    /* Global process list */
    struct list_head proc_list;
};

/*
 * Current thread/process access
 */
extern struct thread *current_thread;

static inline struct thread *get_current(void)
{
    return current_thread;
}

static inline struct process *get_current_process(void)
{
    struct thread *t = get_current();
    return t ? t->process : NULL;
}

#define current get_current()

/*
 * Process management API
 */

/* Initialize process subsystem */
void process_init(void);

/* Create a new process */
struct process *process_create(const char *name);

/* Fork current process */
pid_t process_fork(void);

/* Exit process */
void process_exit(int code) __noreturn;

/* Wait for child process */
pid_t process_wait(int *status);

/* Get process by PID */
struct process *process_find(pid_t pid);

/* Kill a process */
int process_kill(pid_t pid, int sig);

/*
 * Thread management API
 */

/* Create a kernel thread */
struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name);

/* Create a user thread in process */
struct thread *thread_create(struct process *proc, u64 entry, u64 stack);

/* Start a thread (add to scheduler) */
void thread_start(struct thread *t);

/* Current thread yields CPU */
void thread_yield(void);

/* Current thread sleeps */
void thread_sleep(void *channel);

/* Wake threads waiting on channel */
void thread_wakeup(void *channel);

/* Thread exits */
void thread_exit(int code) __noreturn;

/* Get thread by TID */
struct thread *thread_find(tid_t tid);

/*
 * Priority and nice values
 */
#define MAX_PRIO        140         /* Total priority levels */
#define MAX_RT_PRIO     100         /* Real-time priorities */
#define MAX_USER_PRIO   (MAX_PRIO - MAX_RT_PRIO)
#define DEFAULT_PRIO    (MAX_RT_PRIO + 20)  /* Default nice 0 */

#define NICE_TO_PRIO(nice)  ((nice) + DEFAULT_PRIO)
#define PRIO_TO_NICE(prio)  ((prio) - DEFAULT_PRIO)

/* Time slice in nanoseconds (default 10ms) */
#define DEFAULT_TIME_SLICE  (10 * 1000 * 1000)

/* Kernel stack size (8KB) */
#define KERNEL_STACK_SIZE   (8 * 1024)

/*
 * PID allocation
 */
#define PID_MAX         32768

pid_t alloc_pid(void);
void free_pid(pid_t pid);

#endif /* _OCEAN_PROCESS_H */
