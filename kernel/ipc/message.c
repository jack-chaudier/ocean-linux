/*
 * Ocean Kernel - IPC Message Passing
 *
 * Synchronous message passing with rendezvous semantics.
 * Supports fast path (register-only) and buffered messages.
 */

#include <ocean/ipc.h>
#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

/* Global IPC state */
static u64 ipc_total_messages = 0;
static u64 ipc_fast_path_count = 0;

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
static struct thread *find_receiver(struct ipc_endpoint *ep)
{
    if (list_empty(&ep->recv_queue)) {
        return NULL;
    }

    struct list_head *node = ep->recv_queue.next;
    struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);

    /* The thread is embedded in thread_ipc which is part of a larger structure */
    /* For now, we store the thread pointer in wait->partner */
    return wait->partner;
}

/*
 * Find a waiting sender on the endpoint
 */
static struct thread *find_sender(struct ipc_endpoint *ep)
{
    if (list_empty(&ep->send_queue)) {
        return NULL;
    }

    struct list_head *node = ep->send_queue.next;
    struct ipc_wait *wait = container_of(node, struct ipc_wait, wait_list);

    return wait->partner;
}

/*
 * IPC Send - send a message to an endpoint
 *
 * If a receiver is waiting, transfer directly and wake it.
 * Otherwise, queue and block until a receiver arrives.
 */
int ipc_send(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    struct thread *self = get_current();
    struct thread *receiver;
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
    receiver = find_receiver(ep);

    if (receiver) {
        /* Direct transfer - receiver is waiting */
        struct ipc_wait *recv_wait = container_of(
            ep->recv_queue.next, struct ipc_wait, wait_list);

        /* Remove receiver from wait queue */
        list_del(&recv_wait->wait_list);

        /* Copy message to receiver's buffer */
        if (recv_wait->msg) {
            copy_message(recv_wait->msg, msg);
        }

        recv_wait->result = IPC_OK;
        recv_wait->partner = self;

        ep->msgs_sent++;
        ipc_total_messages++;
        ipc_fast_path_count++;

        spin_unlock(&ep->lock);

        /* Wake up receiver */
        sched_wakeup(receiver);

        kprintf("[ipc] Send: direct transfer to TID %d\n", receiver->tid);
    } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_NONBLOCK) {
        /* Non-blocking and no receiver - return immediately */
        spin_unlock(&ep->lock);
        return IPC_ERR_NOPARTNER;
    } else {
        /* Block and wait for receiver */
        struct ipc_wait wait;
        wait.endpoint = ep;
        wait.msg = msg;
        wait.partner = self;
        wait.operation = IPC_OP_SEND;
        wait.result = IPC_ERR_NOPARTNER;
        INIT_LIST_HEAD(&wait.wait_list);

        /* Add to send queue */
        list_add_tail(&wait.wait_list, &ep->send_queue);

        spin_unlock(&ep->lock);

        /* Sleep until a receiver arrives */
        kprintf("[ipc] Send: blocking TID %d\n", self->tid);
        thread_sleep(&wait);

        /* Woken up - check result */
        result = wait.result;
        kprintf("[ipc] Send: TID %d woke, result=%d\n", self->tid, result);
    }

    return result;
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
    struct thread *sender;
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
    sender = find_sender(ep);

    if (sender) {
        /* Direct transfer - sender is waiting */
        struct ipc_wait *send_wait = container_of(
            ep->send_queue.next, struct ipc_wait, wait_list);

        /* Remove sender from wait queue */
        list_del(&send_wait->wait_list);

        /* Copy message from sender's buffer */
        if (send_wait->msg) {
            copy_message(msg, send_wait->msg);
        }

        send_wait->result = IPC_OK;
        send_wait->partner = self;

        ep->msgs_received++;
        ipc_total_messages++;

        spin_unlock(&ep->lock);

        /* Wake up sender */
        sched_wakeup(sender);

        kprintf("[ipc] Recv: direct transfer from TID %d\n", sender->tid);
    } else if (MSG_FLAGS(msg->tag) & MSG_FLAG_NONBLOCK) {
        /* Non-blocking and no sender */
        spin_unlock(&ep->lock);
        return IPC_ERR_NOPARTNER;
    } else {
        /* Block and wait for sender */
        struct ipc_wait wait;
        wait.endpoint = ep;
        wait.msg = msg;
        wait.partner = self;
        wait.operation = IPC_OP_RECV;
        wait.result = IPC_ERR_NOPARTNER;
        INIT_LIST_HEAD(&wait.wait_list);

        /* Add to receive queue */
        list_add_tail(&wait.wait_list, &ep->recv_queue);

        spin_unlock(&ep->lock);

        /* Sleep until a sender arrives */
        kprintf("[ipc] Recv: blocking TID %d\n", self->tid);
        thread_sleep(&wait);

        /* Woken up - check result */
        result = wait.result;
        kprintf("[ipc] Recv: TID %d woke, result=%d\n", self->tid, result);
    }

    return result;
}

/*
 * IPC Call - send message and wait for reply (RPC)
 *
 * This is the typical client operation: send request, get response.
 */
int ipc_call(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    int result;

    /* Send the message */
    result = ipc_send(ep, msg);
    if (result != IPC_OK) {
        return result;
    }

    /* Wait for reply on our reply endpoint */
    /* TODO: Implement reply endpoint */
    /* For now, we can't do proper call semantics */

    return IPC_OK;
}

/*
 * IPC Reply - reply to a caller
 *
 * Sends a reply back to whoever called us.
 */
int ipc_reply(struct ipc_message *msg)
{
    /* TODO: Reply to the calling thread */
    /* Requires tracking who called us */
    (void)msg;
    return IPC_OK;
}

/*
 * IPC Reply + Receive - reply and wait for next message
 *
 * This is the typical server loop operation.
 */
int ipc_reply_recv(struct ipc_endpoint *ep, struct ipc_message *msg)
{
    /* Reply to current caller */
    ipc_reply(msg);

    /* Wait for next message */
    return ipc_recv(ep, msg);
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
