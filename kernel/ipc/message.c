/*
 * Ocean Kernel - IPC Message Passing
 *
 * Synchronous message passing with rendezvous semantics.
 * Supports fast path (register-only) and buffered messages.
 */

#include <ocean/ipc.h>
#include <ocean/ipc_proto.h>
#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Global IPC state */
static u64 ipc_total_messages = 0;
static u64 ipc_fast_path_count = 0;

/*
 * Lock protecting call/reply linkage: a thread's ipc_caller, its peer's
 * ipc_reply_server, and the reply slots. Single coarse lock keeps the
 * teardown paths in thread_exit simple.
 */
spinlock_t ipc_cc_lock = SPINLOCK_INIT;

/*
 * Copy a slice from one process's IPC window into another's at the same
 * offset. Returns IPC_OK on success, or an error if the slice is out of
 * bounds or either process lacks a window. A len of 0 is a no-op success.
 */
static int copy_ipc_slice(struct process *src_proc, struct process *dst_proc,
                          u32 off, u32 len)
{
    if (len == 0) {
        return IPC_OK;
    }
    if (!ipc_slice_valid(off, len)) {
        return IPC_ERR_OVERFLOW;
    }

    void *src = process_ipc_window_kva(src_proc);
    void *dst = process_ipc_window_kva(dst_proc);
    if (!src || !dst) {
        return IPC_ERR_INVALID;
    }

    memcpy((char *)dst + off, (const char *)src + off, len);
    return IPC_OK;
}

/*
 * If a message carries MSG_FLAG_SLICE, interpret regs[0] as a packed
 * (off, len) slice and copy the corresponding bytes between sender and
 * receiver windows. Caller supplies sender_proc and receiver_proc.
 */
static int maybe_copy_window(struct process *sender_proc,
                             struct process *receiver_proc,
                             const struct ipc_message *msg)
{
    if (!(MSG_FLAGS(msg->tag) & MSG_FLAG_SLICE)) {
        return IPC_OK;
    }

    u32 off = (u32)(msg->regs[0] >> 32);
    u32 len = (u32)(msg->regs[0]);
    return copy_ipc_slice(sender_proc, receiver_proc, off, len);
}

/*
 * Initialize IPC subsystem
 */
void ipc_init(void)
{
    kprintf("Initializing IPC subsystem...\n");
    ipc_total_messages = 0;
    ipc_fast_path_count = 0;
    kprintf("IPC subsystem initialized\n");
}

/*
 * Copy message data between threads
 */
static void copy_message(struct ipc_message *dst, struct ipc_message *src)
{
    dst->tag = src->tag;

    /* Copy register data */
    for (int i = 0; i < IPC_FAST_REGS; i++) {
        dst->regs[i] = src->regs[i];
    }

    /* Copy extended buffer if present */
    if (src->buffer && src->buffer_len > 0 && dst->buffer) {
        size_t copy_len = src->buffer_len;
        if (copy_len > dst->buffer_len) {
            copy_len = dst->buffer_len;
        }
        memcpy(dst->buffer, src->buffer, copy_len);
    }

    /* TODO: Handle capability transfer */
    dst->nr_caps = 0;
}

/*
 * Find a waiting receiver on the endpoint
 */
static struct ipc_wait *peek_waiter(struct list_head *queue)
{
    if (list_empty(queue)) {
        return NULL;
    }

    struct list_head *node = queue->next;
    struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);
    return wait;
}

/*
 * Record the client/server pointers for a call so either peer's teardown
 * can break the link. Must be called while the caller-side wake is
 * pending (i.e. before the caller actually returns from ipc_send).
 */
static void link_call(struct thread *caller, struct thread *server)
{
    spin_lock(&ipc_cc_lock);
    caller->ipc_reply_pending = 1;
    caller->ipc_reply_result = IPC_ERR_DEAD;
    caller->ipc_reply_server = server;
    server->ipc_caller = caller;
    spin_unlock(&ipc_cc_lock);
}

/*
 * Shared core of ipc_send and ipc_call.
 *
 * `op` is IPC_OP_SEND for a one-way send, or IPC_OP_CALL for the send half
 * of a call. On a CALL, when the message is transferred (either directly
 * to a waiting receiver or by a receiver dequeueing our blocked send), we
 * link up the caller/server pointers BEFORE waking the peer so that the
 * reply path can find them.
 *
 * Window slices are copied here too: if the tag has MSG_FLAG_SLICE, the
 * copy happens at the same rendezvous as the register data, so the
 * receiver sees both halves atomically.
 */
static int ipc_send_op(struct ipc_endpoint *ep, struct ipc_message *msg, int op)
{
    struct thread *self = get_current();
    int result = IPC_OK;

    if (!ep || !msg) {
        return IPC_ERR_INVALID;
    }

    spin_lock(&ep->lock);

    /* Check if endpoint is dead */
    if (ep->flags & EP_FLAG_DEAD) {
        spin_unlock(&ep->lock);
        return IPC_ERR_DEAD;
    }

    /* Look for a waiting receiver */
    struct ipc_wait *recv_wait = peek_waiter(&ep->recv_queue);

    if (recv_wait) {
        /* Direct transfer - receiver is waiting */
        struct thread *receiver = recv_wait->partner;

        /* Copy any window slice before handing the message over. If the
         * copy fails the message does not cross the boundary. */
        int slice_err = IPC_OK;
        if (self->process && receiver->process) {
            slice_err = maybe_copy_window(self->process, receiver->process, msg);
        } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_SLICE) {
            /* One side has no address space (kernel thread). Reject to avoid
             * silent data loss. */
            slice_err = IPC_ERR_INVALID;
        }

        if (slice_err != IPC_OK) {
            spin_unlock(&ep->lock);
            return slice_err;
        }

        /* Remove receiver from wait queue */
        list_del_init(&recv_wait->wait_list);

        /* Copy message to receiver's buffer */
        if (recv_wait->msg) {
            copy_message(recv_wait->msg, msg);
        }

        /* Tell the receiver's ipc_recv whether this was a call. */
        recv_wait->operation = op;
        recv_wait->result = IPC_OK;
        recv_wait->partner = self;

        ep->msgs_sent++;
        ipc_total_messages++;
        ipc_fast_path_count++;

        if (op == IPC_OP_CALL) {
            link_call(self, receiver);
        }

        spin_unlock(&ep->lock);

        /* Wake up receiver */
        sched_wakeup(receiver);

        kprintf("[ipc] Send: direct transfer to TID %d (op=%d)\n",
                receiver->tid, op);
    } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_NONBLOCK) {
        /* Non-blocking and no receiver - return immediately */
        spin_unlock(&ep->lock);
        return IPC_ERR_NOPARTNER;
    } else {
        /* Block and wait for receiver */
        struct ipc_wait *wait = kmalloc(sizeof(*wait));
        if (!wait) {
            spin_unlock(&ep->lock);
            return IPC_ERR_BUSY;
        }

        wait->endpoint = ep;
        wait->msg = msg;
        wait->partner = self;
        wait->operation = op;
        wait->result = IPC_ERR_NOPARTNER;
        INIT_LIST_HEAD(&wait->wait_list);

        /* Add to send queue */
        list_add_tail(&wait->wait_list, &ep->send_queue);

        spin_unlock(&ep->lock);

        /* Sleep until a receiver arrives */
        kprintf("[ipc] Send: blocking TID %d (op=%d)\n", self->tid, op);
        thread_sleep(wait);

        /*
         * Defensive cleanup for spurious wakeups: remove from queue if still linked.
         */
        spin_lock(&ep->lock);
        if (!list_empty(&wait->wait_list)) {
            list_del_init(&wait->wait_list);
        }
        spin_unlock(&ep->lock);

        /* Woken up - check result */
        result = wait->result;
        kprintf("[ipc] Send: TID %d woke, result=%d\n", self->tid, result);
        kfree(wait);
    }

    return result;
}

int ipc_send(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    return ipc_send_op(ep, msg, IPC_OP_SEND);
}

/*
 * IPC Receive - receive a message from an endpoint
 *
 * If a sender is waiting, receive from it directly.
 * Otherwise, queue and block until a sender arrives.
 */
int ipc_recv(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    struct thread *self = get_current();
    int result = IPC_OK;

    if (!ep || !msg) {
        return IPC_ERR_INVALID;
    }

    spin_lock(&ep->lock);

    /* Check if endpoint is dead */
    if (ep->flags & EP_FLAG_DEAD) {
        spin_unlock(&ep->lock);
        return IPC_ERR_DEAD;
    }

    /* Look for a waiting sender */
    struct ipc_wait *send_wait = peek_waiter(&ep->send_queue);

    if (send_wait) {
        /* Direct transfer - sender is waiting */
        struct thread *sender = send_wait->partner;

        /* Copy any window slice the sender referenced. */
        int slice_err = IPC_OK;
        if (send_wait->msg && sender->process && self->process) {
            slice_err = maybe_copy_window(sender->process, self->process,
                                          send_wait->msg);
        } else if (send_wait->msg &&
                   (MSG_FLAGS(send_wait->msg->tag) & MSG_FLAG_SLICE)) {
            slice_err = IPC_ERR_INVALID;
        }

        if (slice_err != IPC_OK) {
            /* Wake the sender with the failure; do not deliver the message. */
            send_wait->result = slice_err;
            list_del_init(&send_wait->wait_list);
            spin_unlock(&ep->lock);
            sched_wakeup(sender);
            return slice_err;
        }

        /* Remove sender from wait queue */
        list_del_init(&send_wait->wait_list);

        /* Copy message from sender's buffer */
        if (send_wait->msg) {
            copy_message(msg, send_wait->msg);
        }

        send_wait->result = IPC_OK;
        send_wait->partner = self;

        ep->msgs_received++;
        ipc_total_messages++;

        /* If the sender was making a call, link the reply pointers before we
         * wake them so the subsequent reply path can find both sides. */
        if (send_wait->operation == IPC_OP_CALL) {
            link_call(sender, self);
        }

        spin_unlock(&ep->lock);

        /* Wake up sender */
        sched_wakeup(sender);

        kprintf("[ipc] Recv: direct transfer from TID %d (op=%d)\n",
                sender->tid, send_wait->operation);
    } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_NONBLOCK) {
        /* Non-blocking and no sender */
        spin_unlock(&ep->lock);
        return IPC_ERR_NOPARTNER;
    } else {
        /* Block and wait for sender */
        struct ipc_wait *wait = kmalloc(sizeof(*wait));
        if (!wait) {
            spin_unlock(&ep->lock);
            return IPC_ERR_BUSY;
        }

        wait->endpoint = ep;
        wait->msg = msg;
        wait->partner = self;
        wait->operation = IPC_OP_RECV;
        wait->result = IPC_ERR_NOPARTNER;
        INIT_LIST_HEAD(&wait->wait_list);

        /* Add to receive queue */
        list_add_tail(&wait->wait_list, &ep->recv_queue);

        spin_unlock(&ep->lock);

        /* Sleep until a sender arrives */
        kprintf("[ipc] Recv: blocking TID %d\n", self->tid);
        thread_sleep(wait);

        /* Defensive cleanup for spurious wakeups. */
        spin_lock(&ep->lock);
        if (!list_empty(&wait->wait_list)) {
            list_del_init(&wait->wait_list);
        }
        spin_unlock(&ep->lock);

        /* Woken up - check result */
        result = wait->result;
        kprintf("[ipc] Recv: TID %d woke, result=%d\n", self->tid, result);
        kfree(wait);
    }

    return result;
}

/*
 * IPC Call - send message and wait for reply (synchronous RPC).
 *
 * Sends via ipc_send_op with op=CALL so the rendezvous path links our
 * ipc_reply_server/ipc_caller pointers. The reply is written into the
 * caller's thread by ipc_reply on the server side; we wait using the
 * state-first-then-check pattern so a wake that lands between our
 * condition check and schedule() is not lost.
 *
 * One call-in-flight per thread. The thread's ipc_caller slot is not used
 * on the client side, so nested calls are OK if their address spaces
 * allow it; nested incoming calls on the server side are NOT: a server
 * that has not yet replied cannot accept another call. This matches the
 * synchronous single-threaded server model.
 */
int ipc_call(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    struct thread *self = get_current();

    if (!ep || !msg) {
        return IPC_ERR_INVALID;
    }

    /* Send the request half. If it fails, no link was established. */
    int send_result = ipc_send_op(ep, msg, IPC_OP_CALL);
    if (send_result != IPC_OK) {
        /* Make sure stale reply state does not confuse a later call. */
        spin_lock(&ipc_cc_lock);
        self->ipc_reply_pending = 0;
        self->ipc_reply_server = NULL;
        spin_unlock(&ipc_cc_lock);
        return send_result;
    }

    /*
     * Block until the reply arrives or the server dies.
     *
     * The state transition and the condition check must be atomic with
     * respect to ipc_reply's "write result, clear pending, wake" sequence,
     * otherwise a wake that lands between the check and schedule() could
     * race with our own schedule() path:
     *
     *   - enqueue_thread_locked already guards against a double list_add
     *     (kernel/sched/core.c), so the run queue will not be corrupted
     *     even in that race.
     *   - but the loop would still need to re-observe the cleared pending
     *     flag under a stable store ordering.
     *
     * Holding ipc_cc_lock across the check+state write gives us that
     * ordering: ipc_reply cannot flip pending to 0 between our read and
     * the state=INTERRUPTIBLE store, so after we release the lock either
     * we will see pending==0 on the next iteration or ipc_reply has not
     * yet run and will see state==INTERRUPTIBLE when it wakes us.
     */
    for (;;) {
        spin_lock(&ipc_cc_lock);
        if (!self->ipc_reply_pending) {
            self->state = TASK_RUNNING;
            self->wait_channel = NULL;
            spin_unlock(&ipc_cc_lock);
            break;
        }
        self->state = TASK_INTERRUPTIBLE;
        self->wait_channel = (void *)(uintptr_t)&self->ipc_reply_pending;
        spin_unlock(&ipc_cc_lock);

        schedule();
    }

    int result;
    spin_lock(&ipc_cc_lock);
    result = self->ipc_reply_result;
    if (result == IPC_OK) {
        msg->tag = self->ipc_reply_tag;
        for (int i = 0; i < IPC_FAST_REGS; i++) {
            msg->regs[i] = self->ipc_reply_regs[i];
        }
    }
    self->ipc_reply_server = NULL;
    spin_unlock(&ipc_cc_lock);

    kprintf("[ipc] Call: TID %d got reply, result=%d\n", self->tid, result);
    return result;
}

/*
 * IPC Reply - reply to the caller we last received from.
 *
 * Grabs our ipc_caller under ipc_cc_lock, writes the reply slots into
 * the caller's thread, clears reply_pending, and wakes the caller. If
 * MSG_FLAG_SLICE is set the kernel copies the referenced window slice
 * from our window into the caller's at the same offset first.
 *
 * Returns IPC_OK on success, IPC_ERR_INVALID if we have no pending
 * caller (either because the caller died first or because ipc_reply is
 * being called without a preceding receive).
 */
int ipc_reply(struct ipc_message *msg)
{
    struct thread *self = get_current();

    if (!msg) {
        return IPC_ERR_INVALID;
    }

    spin_lock(&ipc_cc_lock);
    struct thread *caller = self->ipc_caller;
    if (!caller) {
        spin_unlock(&ipc_cc_lock);
        return IPC_ERR_INVALID;
    }
    /*
     * Detach now under the lock so teardown paths cannot race us on the
     * caller pointer. We still need to write the reply slots, which we do
     * on the caller's struct under the same lock.
     */
    self->ipc_caller = NULL;

    /* Copy window slice if requested, while holding the lock so the
     * caller's window cannot disappear (process teardown waits for the
     * lock during ipc_destroy_owned_by_process? no, but the caller
     * thread is blocked and will not exit until we wake it). */
    int slice_err = IPC_OK;
    if (self->process && caller->process) {
        slice_err = maybe_copy_window(self->process, caller->process, msg);
    } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_SLICE) {
        slice_err = IPC_ERR_INVALID;
    }

    if (slice_err != IPC_OK) {
        caller->ipc_reply_result = slice_err;
    } else {
        caller->ipc_reply_result = IPC_OK;
        caller->ipc_reply_tag = msg->tag;
        for (int i = 0; i < IPC_FAST_REGS; i++) {
            caller->ipc_reply_regs[i] = msg->regs[i];
        }
    }
    caller->ipc_reply_pending = 0;

    /* Keep ipc_cc_lock held across sched_wakeup so the caller's wait loop
     * cannot sneak in between our pending-clear and the wake: either the
     * caller is still waiting to take the lock (sees pending==0 on the
     * next loop iteration) or already asleep (sched_wakeup flips the
     * INTERRUPTIBLE state before we release). */
    sched_wakeup(caller);
    spin_unlock(&ipc_cc_lock);

    kprintf("[ipc] Reply: TID %d -> TID %d (err=%d)\n",
            self->tid, caller->tid, slice_err);
    return IPC_OK;
}

/*
 * IPC Reply + Receive - reply to the current caller, then block for the
 * next message. The typical top of a server loop.
 */
int ipc_reply_recv(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    ipc_reply(msg);
    return ipc_recv(ep, msg);
}

/*
 * Sever call/reply links for a thread that is about to exit.
 *
 * Two directions to handle:
 *
 *   1. We owe a reply (ipc_caller != NULL). That caller is blocked on their
 *      ipc_reply_pending. Wake them with IPC_ERR_DEAD so they don't stall.
 *
 *   2. We are blocked on a reply (ipc_reply_server != NULL). The server is
 *      still running but holds a pointer back to us. Clear their ipc_caller
 *      so a subsequent ipc_reply sees "no caller" instead of dereferencing
 *      a dead thread.
 *
 * Both directions are maintained under ipc_cc_lock.
 */
void ipc_thread_cleanup(struct thread *t)
{
    if (!t) {
        return;
    }

    spin_lock(&ipc_cc_lock);

    /* Case 1: we owe a reply. Wake the caller with IPC_ERR_DEAD while
     * holding the lock so an interleaving ipc_call wait cannot race
     * pending=0 against our wake. */
    if (t->ipc_caller) {
        struct thread *caller = t->ipc_caller;
        t->ipc_caller = NULL;
        if (caller->ipc_reply_server == t) {
            caller->ipc_reply_server = NULL;
        }
        caller->ipc_reply_result = IPC_ERR_DEAD;
        caller->ipc_reply_pending = 0;
        sched_wakeup(caller);
    }

    /* Case 2: we are waiting on a server. Null out the server's back
     * pointer so a later ipc_reply on that server sees "no caller"
     * instead of dereferencing our about-to-be-freed struct. */
    if (t->ipc_reply_server) {
        struct thread *server = t->ipc_reply_server;
        t->ipc_reply_server = NULL;
        if (server->ipc_caller == t) {
            server->ipc_caller = NULL;
        }
    }

    t->ipc_reply_pending = 0;
    spin_unlock(&ipc_cc_lock);
}

/*
 * Fast path send - register-only message
 *
 * Optimized path for small messages that fit in registers.
 * Attempts direct thread switch if possible.
 */
int ipc_send_fast(u32 ep_id, u64 tag, u64 *regs)
{
    struct ipc_endpoint *ep;
    struct ipc_message msg;

    ep = endpoint_get(ep_id);
    if (!ep) {
        return IPC_ERR_INVALID;
    }

    /* Build message from registers */
    msg.tag = tag;
    for (int i = 0; i < IPC_FAST_REGS; i++) {
        msg.regs[i] = regs[i];
    }
    msg.buffer = NULL;
    msg.buffer_len = 0;
    msg.nr_caps = 0;

    int result = ipc_send(ep, &msg);

    endpoint_put(ep);

    return result;
}

/*
 * Fast path receive - register-only message
 */
int ipc_recv_fast(u32 ep_id, u64 *tag, u64 *regs)
{
    struct ipc_endpoint *ep;
    struct ipc_message msg;

    ep = endpoint_get(ep_id);
    if (!ep) {
        return IPC_ERR_INVALID;
    }

    /* Prepare message buffer */
    msg.tag = 0;
    memset(msg.regs, 0, sizeof(msg.regs));
    msg.buffer = NULL;
    msg.buffer_len = 0;
    msg.nr_caps = 0;

    int result = ipc_recv(ep, &msg);

    if (result == IPC_OK) {
        *tag = msg.tag;
        for (int i = 0; i < IPC_FAST_REGS; i++) {
            regs[i] = msg.regs[i];
        }
    }

    endpoint_put(ep);

    return result;
}
