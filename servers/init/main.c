/*
 * Ocean Init Server
 *
 * The first userspace process (PID 1).
 * Responsible for:
 *   - Parsing the initrd
 *   - Starting core system servers
 *   - Providing basic process lifecycle management
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>

/* Version */
#define INIT_VERSION "0.1.0"

/*
 * Print a message with a prefix
 */
static void init_log(const char *msg)
{
    printf("[init] %s\n", msg);
}

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
 * Initialize basic services
 */
static void init_services(void)
{
    init_log("Starting core services...");

    /* Create an IPC endpoint for init */
    int ep = endpoint_create(0);
    if (ep > 0) {
        printf("[init] Created endpoint %d\n", ep);
    } else {
        init_log("Failed to create endpoint");
    }

    /* TODO: Start core servers:
     *   - Memory server
     *   - Process server
     *   - VFS server
     */

    init_log("Core services started");
}

/*
 * Main idle loop
 */
static void idle_loop(void)
{
    int count = 0;

    init_log("Entering idle loop");

    while (1) {
        /* Yield to other processes */
        yield();

        count++;
        if (count % 100 == 0) {
            printf("[init] Idle tick %d\n", count);
        }

        /* Exit after some iterations for testing */
        if (count >= 300) {
            break;
        }
    }
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

    init_services();

    /* Short idle loop for testing */
    printf("[init] Entering idle loop\n");
    for (int i = 0; i < 10; i++) {
        yield();
    }

    printf("[init] Init exiting\n");

    return 0;
}
