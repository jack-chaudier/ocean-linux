/*
 * Ocean Kernel - IPC Endpoints
 *
 * Endpoints are rendezvous points for synchronous message passing.
 * Threads can send to or receive from endpoints.
 */

#include <ocean/ipc.h>
#include <ocean/ipc_proto.h>
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

/*
 * Endpoint ID counter. IDs in [EP_WKE_MIN, EP_WKE_MAX] are reserved for
 * well-known endpoints claimed by services; dynamic allocations start above
 * that range so they cannot collide.
 */
static u32 next_endpoint_id = EP_WKE_MAX + 1;

/*
 * Allocate a new dynamic endpoint ID (skips the reserved WKE range).
 */
static u32 alloc_endpoint_id(void)
{
    u32 id;
    spin_lock(&endpoint_list_lock);
    if (next_endpoint_id <= EP_WKE_MAX) {
        next_endpoint_id = EP_WKE_MAX + 1;
    }
    id = next_endpoint_id++;
    spin_unlock(&endpoint_list_lock);
    return id;
}

/*
 * Add endpoint to its owner's owned-endpoint list (if it has an owner).
 */
static void link_to_owner(struct ipc_endpoint *ep)
{
    struct process *owner = ep->owner;
    if (!owner) {
        INIT_LIST_HEAD(&ep->owner_link);
        return;
    }

    u64 flags;
    spin_lock_irqsave(&owner->lock, &flags);
    list_add_tail(&ep->owner_link, &owner->owned_endpoints);
    spin_unlock_irqrestore(&owner->lock, flags);
}

static void unlink_from_owner(struct ipc_endpoint *ep)
{
    struct process *owner = ep->owner;
    if (!owner) {
        return;
    }

    u64 flags;
    spin_lock_irqsave(&owner->lock, &flags);
    if (!list_empty(&ep->owner_link)) {
        list_del_init(&ep->owner_link);
    }
    spin_unlock_irqrestore(&owner->lock, flags);
}

/* Search the global list for an endpoint with the given id. Caller holds the
 * endpoint_list_lock. Skips entries already marked dead. */
static struct ipc_endpoint *find_endpoint_locked(u32 id)
{
    struct list_head *node;
    list_for_each(node, &endpoint_list) {
        struct ipc_endpoint *e = container_of(node, struct ipc_endpoint, list);
        if (e->id == id && !(e->flags & EP_FLAG_DEAD)) {
            return e;
        }
    }
    return NULL;
}

/*
 * Common endpoint initialization (id already chosen).
 */
static struct ipc_endpoint *init_endpoint(struct process *owner, u32 id, u32 flags)
{
    struct ipc_endpoint *ep = kmalloc(sizeof(*ep));
    if (!ep) {
        return NULL;
    }

    memset(ep, 0, sizeof(*ep));

    ep->id = id;
    ep->flags = flags | EP_FLAG_LISTED;
    ep->owner = owner;
    ep->refcount = 1;

    spin_init(&ep->lock);
    INIT_LIST_HEAD(&ep->send_queue);
    INIT_LIST_HEAD(&ep->recv_queue);
    INIT_LIST_HEAD(&ep->list);
    INIT_LIST_HEAD(&ep->owner_link);

    return ep;
}

/*
 * Create a new IPC endpoint with a dynamically-allocated id.
 */
struct ipc_endpoint *endpoint_create(struct process *owner, u32 flags)
{
    u32 id = alloc_endpoint_id();

    struct ipc_endpoint *ep = init_endpoint(owner, id, flags);
    if (!ep) {
        return NULL;
    }

    spin_lock(&endpoint_list_lock);
    list_add(&ep->list, &endpoint_list);
    spin_unlock(&endpoint_list_lock);

    link_to_owner(ep);

    kprintf("[ipc] Created endpoint %u (flags=0x%x)\n", ep->id, flags);

    return ep;
}

/*
 * Claim a reserved well-known endpoint id.
 *
 * Returns NULL if the id is outside the reserved range or already claimed.
 * The id check and insertion are performed atomically under the global
 * endpoint list lock to prevent races between two services racing to claim
 * the same WKE.
 */
struct ipc_endpoint *endpoint_create_well_known(struct process *owner,
                                                u32 id, u32 flags)
{
    if (id < EP_WKE_MIN || id > EP_WKE_MAX) {
        return NULL;
    }

    struct ipc_endpoint *ep = init_endpoint(owner, id, flags);
    if (!ep) {
        return NULL;
    }

    spin_lock(&endpoint_list_lock);
    if (find_endpoint_locked(id)) {
        spin_unlock(&endpoint_list_lock);
        kfree(ep);
        return NULL;
    }
    list_add(&ep->list, &endpoint_list);
    spin_unlock(&endpoint_list_lock);

    link_to_owner(ep);

    kprintf("[ipc] Claimed well-known endpoint %u (flags=0x%x)\n", id, flags);

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

    bool remove_from_list = false;

    spin_lock(&ep->lock);

    if (ep->flags & EP_FLAG_DEAD) {
        spin_unlock(&ep->lock);
        return;
    }

    /* Mark as dead */
    ep->flags |= EP_FLAG_DEAD;
    if (ep->flags & EP_FLAG_LISTED) {
        ep->flags &= ~EP_FLAG_LISTED;
        remove_from_list = true;
    }

    /* Wake all senders with error */
    while (!list_empty(&ep->send_queue)) {
        struct list_head *node = ep->send_queue.next;
        struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);
        list_del_init(node);
        wait->result = IPC_ERR_DEAD;
        if (wait->partner) {
            sched_wakeup(wait->partner);
        }
    }

    /* Wake all receivers with error */
    while (!list_empty(&ep->recv_queue)) {
        struct list_head *node = ep->recv_queue.next;
        struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);
        list_del_init(node);
        wait->result = IPC_ERR_DEAD;
        if (wait->partner) {
            sched_wakeup(wait->partner);
        }
    }

    spin_unlock(&ep->lock);

    if (remove_from_list) {
        spin_lock(&endpoint_list_lock);
        if (!list_empty(&ep->list)) {
            list_del_init(&ep->list);
        }
        spin_unlock(&endpoint_list_lock);

        unlink_from_owner(ep);

        /*
         * Drop the list/owner reference from endpoint_create().
         * Memory is actually freed when the final holder drops via endpoint_put().
         */
        endpoint_put(ep);
    }

    kprintf("[ipc] Endpoint %u marked dead\n", ep->id);
}

/*
 * Destroy every endpoint whose owner is the given process.
 *
 * Used during process teardown so dead servers do not leak endpoint IDs
 * (especially well-known ones) and their waiters are woken with IPC_ERR_DEAD
 * instead of blocking forever.
 */
void ipc_destroy_owned_by_process(struct process *proc)
{
    if (!proc) {
        return;
    }

    for (;;) {
        struct ipc_endpoint *ep = NULL;

        u64 flags;
        spin_lock_irqsave(&proc->lock, &flags);
        if (!list_empty(&proc->owned_endpoints)) {
            ep = list_first_entry(&proc->owned_endpoints,
                                  struct ipc_endpoint, owner_link);
            /* Take a reference and drop the owner link while we hold proc->lock
             * so that endpoint_destroy's unlink_from_owner is a no-op. */
            __atomic_fetch_add(&ep->refcount, 1, __ATOMIC_RELAXED);
            list_del_init(&ep->owner_link);
        }
        spin_unlock_irqrestore(&proc->lock, flags);

        if (!ep) {
            break;
        }

        endpoint_destroy(ep);
        endpoint_put(ep);
    }
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
            __atomic_fetch_add(&e->refcount, 1, __ATOMIC_RELAXED);
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

    int refs = __atomic_sub_fetch(&ep->refcount, 1, __ATOMIC_ACQ_REL);
    if (refs == 0) {
        kprintf("[ipc] Destroyed endpoint %u\n", ep->id);
        kfree(ep);
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
    if (ep->flags & EP_FLAG_LISTED) kprintf(" LISTED");
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
