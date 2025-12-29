/*
 * Ocean Init Server
 *
 * The first userspace process (PID 1).
 * Responsible for:
 *   - Starting core system servers
 *   - Managing server lifecycle
 *   - Providing a service registry
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>
#include <ocean/ipc_proto.h>

/* Version */
#define INIT_VERSION "0.2.0"

/* Maximum services we track */
#define MAX_SERVICES 16

/* Service states */
#define SVC_STOPPED     0
#define SVC_STARTING    1
#define SVC_RUNNING     2
#define SVC_STOPPING    3
#define SVC_FAILED      4

/* Service entry */
struct service {
    const char *name;           /* Service name */
    const char *path;           /* Binary path */
    uint32_t    well_known_ep;  /* Well-known endpoint ID */
    int         state;          /* Current state */
    int         pid;            /* Process ID (if running) */
    int         endpoint;       /* Actual endpoint */
    int         priority;       /* Start priority (lower = earlier) */
};

/* Core services - started in priority order */
static struct service services[MAX_SERVICES] = {
    /* Priority 0: Memory management (needed for everything) */
    {
        .name = "mem",
        .path = "/boot/mem.elf",
        .well_known_ep = EP_MEM,
        .state = SVC_STOPPED,
        .priority = 0,
    },
    /* Priority 1: Process management */
    {
        .name = "proc",
        .path = "/boot/proc.elf",
        .well_known_ep = EP_PROC,
        .state = SVC_STOPPED,
        .priority = 1,
    },
    /* Priority 2: Filesystem */
    {
        .name = "vfs",
        .path = "/boot/vfs.elf",
        .well_known_ep = EP_VFS,
        .state = SVC_STOPPED,
        .priority = 2,
    },
    /* Priority 2: RAM filesystem driver */
    {
        .name = "ramfs",
        .path = "/boot/ramfs.elf",
        .well_known_ep = 0,  /* No well-known endpoint */
        .state = SVC_STOPPED,
        .priority = 2,
    },
    /* End marker */
    { .name = NULL }
};

static int init_endpoint = -1;
static int num_running = 0;

/*
 * Print startup banner
 */
static void print_banner(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  Ocean Init Server v%s\n", INIT_VERSION);
    printf("========================================\n");
    printf("\n");
}

/*
 * Log message with prefix
 */
static void init_log(const char *msg)
{
    printf("[init] %s\n", msg);
}

/*
 * Log formatted message
 */
static void init_logf(const char *fmt, const char *arg)
{
    printf("[init] ");
    printf(fmt, arg);
    printf("\n");
}

/*
 * Find service by name
 */
static struct service *find_service(const char *name)
{
    for (int i = 0; i < MAX_SERVICES && services[i].name; i++) {
        if (strcmp(services[i].name, name) == 0) {
            return &services[i];
        }
    }
    return NULL;
}

/*
 * Start a service
 *
 * NOTE: In a full implementation, this would:
 * 1. Load the ELF binary from the filesystem
 * 2. Create a new process with its own address space
 * 3. Start the process running
 *
 * For now, we just simulate service startup since
 * the kernel doesn't yet support dynamic process creation
 * beyond the init process.
 */
static int start_service(struct service *svc)
{
    if (!svc || svc->state == SVC_RUNNING) {
        return -1;
    }

    init_logf("Starting service: %s", svc->name);
    svc->state = SVC_STARTING;

    /* TODO: Actually spawn the service process
     *
     * This would involve:
     * 1. Sending a spawn request to the process server (once it's running)
     * 2. Or using a direct kernel syscall for early boot
     *
     * For now, simulate by creating an endpoint for the service
     */

    svc->endpoint = endpoint_create(0);
    if (svc->endpoint < 0) {
        printf("[init] Failed to create endpoint for %s\n", svc->name);
        svc->state = SVC_FAILED;
        return -1;
    }

    /* Simulate successful start */
    svc->state = SVC_RUNNING;
    svc->pid = 1 + num_running;  /* Fake PID for now */
    num_running++;

    printf("[init] Service '%s' started (simulated) with endpoint %d\n",
           svc->name, svc->endpoint);

    return 0;
}

/*
 * Start all services in priority order
 */
static void start_all_services(void)
{
    init_log("Starting core services...");

    /* Find maximum priority */
    int max_priority = 0;
    for (int i = 0; i < MAX_SERVICES && services[i].name; i++) {
        if (services[i].priority > max_priority) {
            max_priority = services[i].priority;
        }
    }

    /* Start services in priority order */
    for (int prio = 0; prio <= max_priority; prio++) {
        printf("[init] === Priority level %d ===\n", prio);

        for (int i = 0; i < MAX_SERVICES && services[i].name; i++) {
            if (services[i].priority == prio) {
                start_service(&services[i]);

                /* Give service time to initialize */
                for (int j = 0; j < 5; j++) {
                    yield();
                }
            }
        }
    }

    printf("[init] All core services started (%d running)\n", num_running);
}

/*
 * Print service status
 */
static void print_service_status(void)
{
    printf("\n[init] Service Status:\n");
    printf("  NAME     STATE     PID  ENDPOINT\n");
    printf("  -------  --------  ---  --------\n");

    for (int i = 0; i < MAX_SERVICES && services[i].name; i++) {
        const char *state_str;
        switch (services[i].state) {
            case SVC_STOPPED:  state_str = "stopped";  break;
            case SVC_STARTING: state_str = "starting"; break;
            case SVC_RUNNING:  state_str = "running";  break;
            case SVC_STOPPING: state_str = "stopping"; break;
            case SVC_FAILED:   state_str = "FAILED";   break;
            default:           state_str = "unknown";  break;
        }

        printf("  %-7s  %-8s  %-3d  %d\n",
               services[i].name,
               state_str,
               services[i].pid,
               services[i].endpoint);
    }
    printf("\n");
}

/*
 * Main idle/event loop
 *
 * In a full implementation, this would:
 * - Wait for IPC messages from services
 * - Handle service registration requests
 * - Monitor service health
 * - Restart failed services
 */
static void main_loop(void)
{
    int tick = 0;

    init_log("Entering main loop");

    while (tick < 100) {
        yield();
        tick++;

        /* Periodic status update */
        if (tick == 50) {
            print_service_status();
        }
    }

    init_log("Main loop complete");
}

/*
 * Initialize init server
 */
static void init_init(void)
{
    /* Create our IPC endpoint */
    init_endpoint = endpoint_create(0);
    if (init_endpoint < 0) {
        init_log("Failed to create init endpoint");
    } else {
        printf("[init] Created init endpoint %d\n", init_endpoint);
    }
}

/*
 * Shutdown sequence
 */
static void shutdown(void)
{
    init_log("Initiating shutdown...");

    /* Stop services in reverse priority order */
    for (int i = MAX_SERVICES - 1; i >= 0; i--) {
        if (services[i].name && services[i].state == SVC_RUNNING) {
            init_logf("Stopping service: %s", services[i].name);
            services[i].state = SVC_STOPPED;
            /* TODO: Actually send shutdown signal to service */
        }
    }

    init_log("Shutdown complete");
}

/*
 * Main entry point
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    print_banner();

    printf("[init] PID: %d, PPID: %d\n", getpid(), getppid());

    init_init();

    start_all_services();

    main_loop();

    shutdown();

    printf("[init] Init server exiting normally\n");

    return 0;
}
