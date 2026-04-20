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
#include <ocean/userspace_manifest.h>

/* Version */
#define INIT_VERSION "0.3.0"

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
    const char *summary;        /* Human-readable description */
    uint32_t    well_known_ep;  /* Well-known endpoint ID */
    int         state;          /* Current state */
    int         pid;            /* Process ID (if running) */
    int         endpoint;       /* Actual endpoint */
    int         priority;       /* Start priority (lower = earlier) */
};

static struct service services[MAX_SERVICES];
static int num_services = 0;

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
    for (int i = 0; i < num_services; i++) {
        if (services[i].priority > max_priority) {
            max_priority = services[i].priority;
        }
    }

    /* Start services in priority order */
    for (int prio = 0; prio <= max_priority; prio++) {
        printf("[init] === Priority level %d ===\n", prio);

        for (int i = 0; i < num_services; i++) {
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
    printf("  NAME     STATE     PID  ENDPOINT  SUMMARY\n");
    printf("  -------  --------  ---  --------  -----------------------------\n");

    for (int i = 0; i < num_services; i++) {
        const char *state_str;
        switch (services[i].state) {
            case SVC_STOPPED:  state_str = "stopped";  break;
            case SVC_STARTING: state_str = "starting"; break;
            case SVC_RUNNING:  state_str = "running";  break;
            case SVC_STOPPING: state_str = "stopping"; break;
            case SVC_FAILED:   state_str = "FAILED";   break;
            default:           state_str = "unknown";  break;
        }

        printf("  %-7s  %-8s  %-3d  %-8d  %s\n",
               services[i].name,
               state_str,
               services[i].pid,
               services[i].endpoint,
               services[i].summary ? services[i].summary : "");
    }
    printf("\n");
}

/*
 * Spawn the interactive shell
 */
static int spawn_shell(void)
{
    const struct ocean_boot_module_spec *shell =
        ocean_find_boot_module_spec("sh");
    char *shell_argv[] = { "sh", NULL };

    if (!shell) {
        init_log("Shell boot module not found in manifest");
        return 1;
    }

    init_log("Spawning shell...");

    int pid = fork();
    if (pid < 0) {
        init_log("Failed to fork for shell");
        return 1;
    }

    if (pid == 0) {
        exec(shell->path, shell_argv, NULL);
        /* If exec returns, it failed */
        printf("[init] exec shell failed: %s\n", shell->path);
        _exit(1);
    }

    printf("[init] Shell spawned with PID %d\n", pid);

    while (1) {
        int status = 0;
        int child_pid = wait(&status);
        if (child_pid == pid) {
            printf("[init] Shell exited with status %d\n", status);
            return status;
        }
        if (child_pid < 0) {
            init_log("wait() failed while monitoring shell");
            return 1;
        }
        yield();
    }
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
    init_log("Starting interactive shell");

    for (int attempt = 0; attempt < 3; attempt++) {
        int status = spawn_shell();
        if (status == 0) {
            init_log("Shell exited cleanly");
            return;
        }

        printf("[init] Shell failed with status %d, restarting (%d/3)\n",
               status, attempt + 1);
    }

    init_log("Shell exceeded restart budget");
}

/*
 * Initialize init server
 */
static void load_service_manifest(void)
{
    memset(services, 0, sizeof(services));
    num_services = 0;

    for (size_t i = 0; i < OCEAN_SERVICE_SPEC_COUNT; i++) {
        if (num_services >= MAX_SERVICES) {
            init_log("Service manifest exceeds MAX_SERVICES");
            break;
        }

        services[num_services].name = ocean_service_specs[i].name;
        services[num_services].path = ocean_service_specs[i].path;
        services[num_services].summary = ocean_service_specs[i].summary;
        services[num_services].well_known_ep = ocean_service_specs[i].well_known_ep;
        services[num_services].state = SVC_STOPPED;
        services[num_services].priority = ocean_service_specs[i].priority;
        num_services++;
    }
}

static void init_init(void)
{
    load_service_manifest();

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
    for (int i = num_services - 1; i >= 0; i--) {
        if (services[i].state == SVC_RUNNING) {
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
    print_service_status();

    main_loop();

    shutdown();

    printf("[init] Init server exiting normally\n");

    return 0;
}
