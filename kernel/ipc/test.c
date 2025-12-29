/*
 * Ocean Kernel - IPC Test
 *
 * Simple tests for the IPC subsystem using kernel threads.
 */

#include <ocean/ipc.h>
#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name);
extern void thread_start(struct thread *t);

/* Test endpoint */
static struct ipc_endpoint *test_ep = NULL;
static volatile int test_done = 0;

/*
 * Receiver thread - waits for messages
 */
static int ipc_receiver_thread(void *arg)
{
    struct ipc_message msg;
    int count = 0;

    (void)arg;

    kprintf("[receiver] Started, waiting for messages...\n");

    while (count < 3) {
        /* Set up receive buffer - blocking */
        msg.tag = MSG_TAG(0, 0, 0, 0);
        msg.buffer = NULL;
        msg.buffer_len = 0;
        msg.nr_caps = 0;

        int result = ipc_recv(test_ep, &msg);

        if (result == IPC_OK) {
            u32 label = MSG_LABEL(msg.tag);
            kprintf("[receiver] Got message: label=%u, data=[0x%llx, 0x%llx]\n",
                    label, msg.regs[0], msg.regs[1]);
            count++;
        } else {
            kprintf("[receiver] Error: %d\n", result);
            break;
        }
    }

    kprintf("[receiver] Done, received %d messages\n", count);
    test_done = 1;

    /* Sleep forever - we're done (use static var as channel) */
    thread_sleep(&test_done);
    return 0;
}

/*
 * Sender thread - sends messages
 */
static int ipc_sender_thread(void *arg)
{
    struct ipc_message msg;
    int i;

    (void)arg;

    kprintf("[sender] Started, sending messages...\n");

    for (i = 0; i < 3; i++) {
        /* Build message */
        msg.tag = MSG_TAG(100 + i, 2, 0, 0);  /* Label, 2 data words */
        msg.regs[0] = 0xCAFE0000 + i;
        msg.regs[1] = 0xDEAD0000 + i;
        msg.buffer = NULL;
        msg.buffer_len = 0;
        msg.nr_caps = 0;

        kprintf("[sender] Sending message %d: label=%u, data=[0x%llx, 0x%llx]\n",
                i, 100 + i, msg.regs[0], msg.regs[1]);

        int result = ipc_send(test_ep, &msg);

        if (result != IPC_OK) {
            kprintf("[sender] Send failed: %d\n", result);
        } else {
            kprintf("[sender] Message %d sent successfully\n", i);
        }
    }

    kprintf("[sender] Done\n");

    /* Sleep forever - we're done */
    thread_sleep(&test_done);
    return 0;
}

/*
 * Test IPC between two kernel threads
 */
void ipc_test(void)
{
    struct thread *sender, *receiver;

    kprintf("\n=== IPC Test ===\n");

    /* Create test endpoint */
    test_ep = endpoint_create(NULL, 0);
    if (!test_ep) {
        kprintf("Failed to create test endpoint!\n");
        return;
    }
    kprintf("Created endpoint %u\n", test_ep->id);

    /* Create receiver thread (starts first to be ready) */
    receiver = kthread_create(ipc_receiver_thread, NULL, "ipc-recv");
    if (!receiver) {
        kprintf("Failed to create receiver thread!\n");
        return;
    }

    /* Create sender thread */
    sender = kthread_create(ipc_sender_thread, NULL, "ipc-send");
    if (!sender) {
        kprintf("Failed to create sender thread!\n");
        return;
    }

    /* Start threads */
    thread_start(receiver);
    thread_start(sender);

    kprintf("IPC test threads started\n");

    /* Wait for test to complete by yielding */
    int timeout = 100;
    while (!test_done && timeout > 0) {
        sched_yield();
        timeout--;
    }

    if (test_done) {
        kprintf("IPC test completed successfully!\n");
    } else {
        kprintf("IPC test timed out\n");
    }

    /* Dump endpoint stats */
    ipc_dump_endpoint(test_ep);
    ipc_dump_stats();

    kprintf("=== IPC Test Done ===\n\n");
}
