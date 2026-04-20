/*
 * Ocean Kernel - IPC Test
 *
 * Simple tests for the IPC subsystem using kernel threads.
 */

#include <ocean/ipc.h>
#include <ocean/ipc_proto.h>
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
    thread_sleep((void *)&test_done);
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
    thread_sleep((void *)&test_done);
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

/*
 * Exercise the well-known endpoint claim path.
 *
 * Checks three things:
 *   1. An out-of-range id is rejected.
 *   2. A valid WKE claim succeeds and the endpoint is looked up by that id.
 *   3. A second claim on the same id fails with the endpoint still live,
 *      and after destruction the id can be re-claimed.
 */
void ipc_test_wke(void)
{
    kprintf("\n=== WKE Claim Test ===\n");

    /* Out-of-range id should be refused. */
    if (endpoint_create_well_known(NULL, EP_WKE_MAX + 1, 0)) {
        kprintf("WKE test FAIL: out-of-range id was accepted\n");
        return;
    }

    /* Claim a reserved id not yet used by any service (EP_RS + 1 = 8). */
    const u32 probe_id = EP_RS + 1;
    struct ipc_endpoint *first = endpoint_create_well_known(NULL, probe_id, 0);
    if (!first) {
        kprintf("WKE test FAIL: first claim of id %u failed\n", probe_id);
        return;
    }
    if (first->id != probe_id) {
        kprintf("WKE test FAIL: claim returned id %u, expected %u\n",
                first->id, probe_id);
        return;
    }

    /* Lookup must succeed and return the same endpoint. */
    struct ipc_endpoint *looked = endpoint_get(probe_id);
    if (!looked || looked != first) {
        kprintf("WKE test FAIL: endpoint_get(%u) did not return the claim\n",
                probe_id);
        return;
    }
    endpoint_put(looked);

    /* Contested claim must fail while the first is still live. */
    struct ipc_endpoint *second = endpoint_create_well_known(NULL, probe_id, 0);
    if (second) {
        kprintf("WKE test FAIL: contested claim of id %u succeeded\n", probe_id);
        return;
    }

    /* Destroy and re-claim. */
    endpoint_destroy(first);
    endpoint_put(first);

    struct ipc_endpoint *third = endpoint_create_well_known(NULL, probe_id, 0);
    if (!third) {
        kprintf("WKE test FAIL: re-claim of id %u after destroy failed\n",
                probe_id);
        return;
    }
    endpoint_destroy(third);
    endpoint_put(third);

    kprintf("WKE test passed.\n");
    kprintf("=== WKE Claim Test Done ===\n\n");
}

/*
 * Call/reply round-trip self-test.
 *
 * Spawns a tiny kthread server on a reserved WKE, sends it three calls
 * from another kthread, verifies each reply echoes the request +1. Covers:
 *   - direct-transfer path (when server reaches ipc_recv first)
 *   - queued-send path (when client's ipc_call runs first)
 *   - caller blocking and wakeup on reply
 */
static struct ipc_endpoint *cr_ep = NULL;
static volatile int cr_server_ready = 0;
static volatile int cr_server_done = 0;
static volatile int cr_client_done = 0;
static volatile int cr_client_pass = 0;
static volatile u64 cr_client_replies[3];

static int cr_server_thread(void *arg)
{
    (void)arg;
    kprintf("[cr-server] started\n");
    cr_server_ready = 1;

    for (int i = 0; i < 3; i++) {
        struct ipc_message req;
        req.tag = 0;
        req.buffer = NULL;
        req.buffer_len = 0;
        req.nr_caps = 0;
        for (int j = 0; j < IPC_FAST_REGS; j++) req.regs[j] = 0;

        int r = ipc_recv(cr_ep, &req);
        if (r != IPC_OK) {
            kprintf("[cr-server] recv %d failed: %d\n", i, r);
            break;
        }

        /* Build reply: echo regs[0] + 1, same label. */
        struct ipc_message rep;
        rep.tag = MSG_TAG(MSG_LABEL(req.tag), 1, 0, 0);
        rep.regs[0] = req.regs[0] + 1;
        for (int j = 1; j < IPC_FAST_REGS; j++) rep.regs[j] = 0;
        rep.buffer = NULL;
        rep.buffer_len = 0;
        rep.nr_caps = 0;

        r = ipc_reply(&rep);
        if (r != IPC_OK) {
            kprintf("[cr-server] reply %d failed: %d\n", i, r);
            break;
        }
    }

    cr_server_done = 1;
    thread_sleep((void *)&cr_server_done);
    return 0;
}

static int cr_client_thread(void *arg)
{
    (void)arg;

    /* Wait for the server to be ready so at least one iteration hits the
     * direct-transfer path (server blocked in recv when we call). */
    while (!cr_server_ready) sched_yield();

    kprintf("[cr-client] started\n");

    int pass = 1;
    for (int i = 0; i < 3; i++) {
        struct ipc_message req;
        req.tag = MSG_TAG(0xA00 + i, 1, 0, 0);
        req.regs[0] = 0xBEEF0000 + i;
        for (int j = 1; j < IPC_FAST_REGS; j++) req.regs[j] = 0;
        req.buffer = NULL;
        req.buffer_len = 0;
        req.nr_caps = 0;

        int r = ipc_call(cr_ep, &req);
        if (r != IPC_OK) {
            kprintf("[cr-client] call %d failed: %d\n", i, r);
            pass = 0;
            break;
        }

        cr_client_replies[i] = req.regs[0];
        if (req.regs[0] != (0xBEEF0000 + i + 1)) {
            kprintf("[cr-client] call %d reply mismatch: got 0x%llx\n",
                    i, (unsigned long long)req.regs[0]);
            pass = 0;
        }
    }

    cr_client_pass = pass;
    cr_client_done = 1;
    thread_sleep((void *)&cr_client_done);
    return 0;
}

void ipc_test_call_reply(void)
{
    kprintf("\n=== Call/Reply Test ===\n");

    cr_ep = endpoint_create_well_known(NULL, EP_RS + 2, 0);
    if (!cr_ep) {
        kprintf("Call/reply test FAIL: could not claim WKE %d\n", EP_RS + 2);
        return;
    }

    cr_server_ready = 0;
    cr_server_done = 0;
    cr_client_done = 0;
    cr_client_pass = 0;

    struct thread *srv = kthread_create(cr_server_thread, NULL, "cr-server");
    struct thread *cli = kthread_create(cr_client_thread, NULL, "cr-client");
    if (!srv || !cli) {
        kprintf("Call/reply test FAIL: kthread_create returned NULL\n");
        return;
    }
    thread_start(srv);
    thread_start(cli);

    int timeout = 200;
    while ((!cr_client_done || !cr_server_done) && timeout > 0) {
        sched_yield();
        timeout--;
    }

    if (!cr_client_done || !cr_server_done) {
        kprintf("Call/reply test FAIL: timeout (client=%d server=%d)\n",
                cr_client_done, cr_server_done);
        return;
    }

    if (!cr_client_pass) {
        kprintf("Call/reply test FAIL: reply mismatch\n");
        return;
    }

    kprintf("Call/reply test passed: replies = 0x%llx 0x%llx 0x%llx\n",
            (unsigned long long)cr_client_replies[0],
            (unsigned long long)cr_client_replies[1],
            (unsigned long long)cr_client_replies[2]);
    kprintf("=== Call/Reply Test Done ===\n\n");
}

/*
 * Print a line summarising whether the given process got its IPC window
 * mapped. Called from the boot path right after init exec's in, so we can
 * tell from a smoke log that the window bringup worked end-to-end.
 */
void ipc_log_window_status(pid_t pid)
{
    struct process *proc = process_find(pid);
    if (!proc) {
        kprintf("[ipc] window status: no process with PID %d\n", pid);
        return;
    }

    if (!proc->ipc_window_phys) {
        kprintf("[ipc] window status: PID %d has NO IPC window\n", pid);
        return;
    }

    kprintf("[ipc] window status: PID %d IPC window at user VA 0x%llx, "
            "phys 0x%llx (size %u)\n",
            pid,
            (unsigned long long)OCEAN_IPC_WINDOW_VA,
            (unsigned long long)proc->ipc_window_phys,
            (unsigned)OCEAN_IPC_WINDOW_SIZE);
}
