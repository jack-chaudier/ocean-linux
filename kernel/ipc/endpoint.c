/*
 * Ocean Kernel - IPC Endpoints
 *
 * Endpoints are rendezvous points for synchronous message passing.
 * Threads can send to or receive from endpoints.
 */

#include <ocean/ipc.h>
#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, size_t n);

/* Global endpoint list */
static struct list_head endpoint_list = LIST_HEAD_INIT(endpoint_list);
static spinlock_t endpoint_list_lock = SPINLOCK_INIT;

/* Endpoint ID counter */
static u32 next_endpoint_id = 1;

/*
 * Allocate a new endpoint ID
 */
static u32 alloc_endpoint_id(void)
{
    u32 id;
    spin_lock(&endpoint_list_lock);
    id = next_endpoint_id++;
    spin_unlock(&endpoint_list_lock);
    return id;
}

/*
 * Create a new IPC endpoint
 */
struct ipc_endpoint *endpoint_create(struct process *owner, u32 flags)
{
    struct ipc_endpoint *ep;

    ep = kmalloc(sizeof(*ep));
    if (!ep) {
        return NULL;
    }

    memset(ep, 0, sizeof(*ep));

    ep->id = alloc_endpoint_id();
    ep->flags = flags;
    ep->owner = owner;
    ep->refcount = 1;

    spin_init(&ep->lock);
    INIT_LIST_HEAD(&ep->send_queue);
    INIT_LIST_HEAD(&ep->recv_queue);

    /* Add to global list */
    spin_lock(&endpoint_list_lock);
    list_add(&ep->list, &endpoint_list);
    spin_unlock(&endpoint_list_lock);

    kprintf("[ipc] Created endpoint %u (flags=0x%x)\n", ep->id, flags);

    return ep;
}

/*
 * Destroy an endpoint
 *
 * Wakes all waiting threads with error.
 */
void endpoint_destroy(struct ipc_endpoint *ep)
{
    if (!ep) {
        return;
    }

    spin_lock(&ep->lock);

    /* Mark as dead */
    ep->flags |= EP_FLAG_DEAD;

    /* Wake all senders with error */
    while (!list_empty(&ep->send_queue)) {
        struct list_head *node = ep->send_queue.next;
        struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);
        list_del(node);
        wait->result = IPC_ERR_DEAD;
        if (wait->partner) {
            sched_wakeup(wait->partner);
        }
    }

    /* Wake all receivers with error */
    while (!list_empty(&ep->recv_queue)) {
        struct list_head *node = ep->recv_queue.next;
        struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);
        list_del(node);
        wait->result = IPC_ERR_DEAD;
        if (wait->partner) {
            sched_wakeup(wait->partner);
        }
    }

    spin_unlock(&ep->lock);

    /* Remove from global list */
    spin_lock(&endpoint_list_lock);
    list_del(&ep->list);
    spin_unlock(&endpoint_list_lock);

    kprintf("[ipc] Destroyed endpoint %u\n", ep->id);

    kfree(ep);
}

/*
 * Get endpoint by ID
 */
struct ipc_endpoint *endpoint_get(u32 id)
{
    struct ipc_endpoint *ep = NULL;

    spin_lock(&endpoint_list_lock);

    struct list_head *node;
    list_for_each(node, &endpoint_list) {
        struct ipc_endpoint *e = container_of(node, struct ipc_endpoint, list);
        if (e->id == id && !(e->flags & EP_FLAG_DEAD)) {
            e->refcount++;
            ep = e;
            break;
        }
    }

    spin_unlock(&endpoint_list_lock);

    return ep;
}

/*
 * Release endpoint reference
 */
void endpoint_put(struct ipc_endpoint *ep)
{
    if (!ep) {
        return;
    }

    spin_lock(&ep->lock);
    ep->refcount--;
    int should_free = (ep->refcount == 0);
    spin_unlock(&ep->lock);

    if (should_free) {
        endpoint_destroy(ep);
    }
}

/*
 * Bind an endpoint to a thread (for reply endpoints)
 */
int endpoint_bind(struct ipc_endpoint *ep, struct thread *t)
{
    if (!ep || !t) {
        return -1;
    }

    spin_lock(&ep->lock);

    if (ep->bound_thread) {
        spin_unlock(&ep->lock);
        return -1;  /* Already bound */
    }

    ep->bound_thread = t;
    ep->flags |= EP_FLAG_BOUND;

    spin_unlock(&ep->lock);

    return 0;
}

/*
 * Unbind an endpoint
 */
int endpoint_unbind(struct ipc_endpoint *ep)
{
    if (!ep) {
        return -1;
    }

    spin_lock(&ep->lock);

    ep->bound_thread = NULL;
    ep->flags &= ~EP_FLAG_BOUND;

    spin_unlock(&ep->lock);

    return 0;
}

/*
 * Check if anyone is waiting to receive on this endpoint
 */
int endpoint_has_receiver(struct ipc_endpoint *ep)
{
    int result;

    spin_lock(&ep->lock);
    result = !list_empty(&ep->recv_queue);
    spin_unlock(&ep->lock);

    return result;
}

/*
 * Check if anyone is waiting to send on this endpoint
 */
int endpoint_has_sender(struct ipc_endpoint *ep)
{
    int result;

    spin_lock(&ep->lock);
    result = !list_empty(&ep->send_queue);
    spin_unlock(&ep->lock);

    return result;
}

/*
 * Dump endpoint state for debugging
 */
void ipc_dump_endpoint(struct ipc_endpoint *ep)
{
    if (!ep) {
        kprintf("  (null endpoint)\n");
        return;
    }

    spin_lock(&ep->lock);

    kprintf("Endpoint %u:\n", ep->id);
    kprintf("  Flags: 0x%x", ep->flags);
    if (ep->flags & EP_FLAG_BOUND) kprintf(" BOUND");
    if (ep->flags & EP_FLAG_REPLY) kprintf(" REPLY");
    if (ep->flags & EP_FLAG_NOTIFICATION) kprintf(" NOTIFICATION");
    if (ep->flags & EP_FLAG_DEAD) kprintf(" DEAD");
    kprintf("\n");

    kprintf("  Refcount: %d\n", ep->refcount);
    kprintf("  Owner: %s (PID %d)\n",
            ep->owner ? ep->owner->name : "(none)",
            ep->owner ? ep->owner->pid : -1);

    if (ep->bound_thread) {
        kprintf("  Bound to: TID %d\n", ep->bound_thread->tid);
    }

    /* Count waiters */
    int send_waiters = 0, recv_waiters = 0;
    struct list_head *node;
    list_for_each(node, &ep->send_queue) {
        send_waiters++;
    }
    list_for_each(node, &ep->recv_queue) {
        recv_waiters++;
    }

    kprintf("  Send queue: %d waiting\n", send_waiters);
    kprintf("  Recv queue: %d waiting\n", recv_waiters);
    kprintf("  Stats: %llu sent, %llu received\n",
            ep->msgs_sent, ep->msgs_received);

    spin_unlock(&ep->lock);
}

/*
 * Dump IPC subsystem statistics
 */
void ipc_dump_stats(void)
{
    int count = 0;
    u64 total_sent = 0, total_recv = 0;

    spin_lock(&endpoint_list_lock);

    struct list_head *node;
    list_for_each(node, &endpoint_list) {
        struct ipc_endpoint *ep = container_of(node, struct ipc_endpoint, list);
        count++;
        total_sent += ep->msgs_sent;
        total_recv += ep->msgs_received;
    }

    spin_unlock(&endpoint_list_lock);

    kprintf("\nIPC Statistics:\n");
    kprintf("  Endpoints: %d\n", count);
    kprintf("  Total messages sent: %llu\n", total_sent);
    kprintf("  Total messages received: %llu\n", total_recv);
}
