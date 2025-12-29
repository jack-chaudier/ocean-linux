/*
 * Ocean Process Server
 *
 * Userspace process management server that handles:
 *   - Process spawning and lifecycle
 *   - File descriptor tables
 *   - Process information queries
 *   - Wait/exit notifications
 *
 * Works with the kernel for actual process creation but
 * manages higher-level process state and relationships.
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

#define PROC_VERSION "0.1.0"
#define MAX_PROCS    64
#define MAX_FDS      32

/* File descriptor entry */
struct fd_entry {
    uint32_t vfs_handle;    /* Handle from VFS server */
    uint32_t flags;         /* Open flags */
};

/* Process entry */
struct proc_entry {
    uint32_t pid;           /* Process ID */
    uint32_t ppid;          /* Parent process ID */
    uint32_t state;         /* Process state */
    uint32_t exit_code;     /* Exit code (if exited) */
    uint32_t endpoint;      /* Process IPC endpoint */
    char     name[32];      /* Process name */
    struct fd_entry fds[MAX_FDS];  /* File descriptor table */
};

/* Process states */
#define PROC_STATE_FREE     0
#define PROC_STATE_RUNNING  1
#define PROC_STATE_WAITING  2
#define PROC_STATE_ZOMBIE   3

static struct proc_entry procs[MAX_PROCS];
static int num_procs = 0;
static int proc_endpoint = -1;
static uint32_t next_pid = 2;   /* PID 1 is init */

/* Statistics */
static uint64_t spawn_count = 0;
static uint64_t exit_count = 0;
static uint64_t wait_count = 0;

/*
 * Find process by PID
 */
static struct proc_entry *find_proc(uint32_t pid)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].pid == pid && procs[i].state != PROC_STATE_FREE) {
            return &procs[i];
        }
    }
    return NULL;
}

/*
 * Find free process slot
 */
static struct proc_entry *alloc_proc(void)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_STATE_FREE) {
            return &procs[i];
        }
    }
    return NULL;
}

/*
 * Initialize the process server
 */
static void proc_init(void)
{
    printf("[proc] Process Server v%s starting\n", PROC_VERSION);

    /* Initialize process table */
    memset(procs, 0, sizeof(procs));

    /* Register init process (PID 1) */
    procs[0].pid = 1;
    procs[0].ppid = 0;
    procs[0].state = PROC_STATE_RUNNING;
    procs[0].endpoint = EP_INIT;
    strncpy(procs[0].name, "init", sizeof(procs[0].name) - 1);
    num_procs = 1;

    /* Create our IPC endpoint */
    proc_endpoint = endpoint_create(0);
    if (proc_endpoint < 0) {
        printf("[proc] Failed to create endpoint: %d\n", proc_endpoint);
        return;
    }
    printf("[proc] Created endpoint %d\n", proc_endpoint);

    printf("[proc] Process server initialized\n");
}

/*
 * Handle PROC_SPAWN request
 */
static int handle_spawn(uint32_t parent_pid, const char *path, uint32_t *out_pid)
{
    spawn_count++;

    struct proc_entry *parent = find_proc(parent_pid);
    if (!parent) {
        printf("[proc] Unknown parent PID %u\n", parent_pid);
        return E_NOENT;
    }

    struct proc_entry *child = alloc_proc();
    if (!child) {
        printf("[proc] No free process slots\n");
        return E_NOMEM;
    }

    /* Initialize the new process entry */
    child->pid = next_pid++;
    child->ppid = parent_pid;
    child->state = PROC_STATE_RUNNING;
    child->exit_code = 0;

    /* Extract name from path */
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') {
            name = p + 1;
        }
        p++;
    }
    strncpy(child->name, name, sizeof(child->name) - 1);

    /* Initialize file descriptors (inherit from parent) */
    memcpy(child->fds, parent->fds, sizeof(child->fds));

    /* TODO: Actually spawn process via kernel syscall */

    num_procs++;
    *out_pid = child->pid;

    printf("[proc] Spawned '%s' as PID %u (parent %u)\n",
           child->name, child->pid, parent_pid);

    return E_OK;
}

/*
 * Handle PROC_EXIT notification
 */
static int handle_exit(uint32_t pid, int exit_code)
{
    exit_count++;

    struct proc_entry *proc = find_proc(pid);
    if (!proc) {
        printf("[proc] Exit from unknown PID %u\n", pid);
        return E_NOENT;
    }

    proc->state = PROC_STATE_ZOMBIE;
    proc->exit_code = (uint32_t)exit_code;

    printf("[proc] PID %u ('%s') exited with code %d\n",
           pid, proc->name, exit_code);

    /* TODO: Notify waiting parent */

    return E_OK;
}

/*
 * Handle PROC_WAIT request
 */
static int handle_wait(uint32_t parent_pid, int32_t wait_pid,
                       uint32_t *out_pid, int *out_status)
{
    wait_count++;

    (void)parent_pid;

    /* Look for a zombie child */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_STATE_ZOMBIE) {
            if (wait_pid == -1 || (uint32_t)wait_pid == procs[i].pid) {
                *out_pid = procs[i].pid;
                *out_status = (int)procs[i].exit_code;

                /* Free the process slot */
                procs[i].state = PROC_STATE_FREE;
                procs[i].pid = 0;
                num_procs--;

                printf("[proc] Reaped zombie PID %u\n", *out_pid);
                return E_OK;
            }
        }
    }

    /* No zombie found */
    *out_pid = 0;
    *out_status = 0;

    /* TODO: Block caller until child exits */

    return E_NOENT;
}

/*
 * Handle PROC_GETINFO request
 */
static int handle_getinfo(uint32_t pid, char *name, size_t name_len,
                          uint32_t *state, uint32_t *ppid)
{
    struct proc_entry *proc = find_proc(pid);
    if (!proc) {
        return E_NOENT;
    }

    if (name && name_len > 0) {
        strncpy(name, proc->name, name_len - 1);
        name[name_len - 1] = '\0';
    }
    *state = proc->state;
    *ppid = proc->ppid;

    return E_OK;
}

/*
 * Process incoming IPC messages
 */
static void proc_serve(void)
{
    printf("[proc] Entering service loop\n");

    /* For now, just yield and do some self-testing */
    for (int i = 0; i < 50; i++) {
        yield();

        /* Simulate spawning a process */
        if (i == 10) {
            uint32_t pid = 0;
            int err = handle_spawn(1, "/bin/sh", &pid);
            if (err == E_OK) {
                printf("[proc] Self-test: spawned PID %u\n", pid);
            }
        }

        /* Simulate process exit */
        if (i == 20) {
            handle_exit(2, 0);
        }

        /* Simulate wait */
        if (i == 30) {
            uint32_t pid = 0;
            int status = 0;
            int err = handle_wait(1, -1, &pid, &status);
            if (err == E_OK) {
                printf("[proc] Self-test: waited on PID %u, status %d\n",
                       pid, status);
            }
        }
    }
}

/*
 * Print process table
 */
static void proc_dump(void)
{
    printf("\n[proc] Process Table:\n");
    printf("  PID   PPID  STATE    NAME\n");
    printf("  ----  ----  -------  ----\n");

    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_STATE_FREE) {
            const char *state_str;
            switch (procs[i].state) {
                case PROC_STATE_RUNNING: state_str = "running"; break;
                case PROC_STATE_WAITING: state_str = "waiting"; break;
                case PROC_STATE_ZOMBIE:  state_str = "zombie";  break;
                default:                 state_str = "unknown"; break;
            }
            printf("  %-4u  %-4u  %-7s  %s\n",
                   procs[i].pid, procs[i].ppid, state_str, procs[i].name);
        }
    }

    printf("\n[proc] Statistics:\n");
    printf("  Active processes: %d\n", num_procs);
    printf("  Spawn requests: %llu\n", (unsigned long long)spawn_count);
    printf("  Exit notifications: %llu\n", (unsigned long long)exit_count);
    printf("  Wait requests: %llu\n", (unsigned long long)wait_count);
    printf("\n");
}

/*
 * Main entry point
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n========================================\n");
    printf("  Ocean Process Server v%s\n", PROC_VERSION);
    printf("========================================\n\n");

    printf("[proc] PID: %d, PPID: %d\n", getpid(), getppid());

    proc_init();
    proc_serve();
    proc_dump();

    printf("[proc] Process server exiting\n");
    return 0;
}
