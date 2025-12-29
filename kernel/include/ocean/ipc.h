/*
 * Ocean Kernel - Inter-Process Communication
 *
 * Synchronous message-passing IPC with capability-based access control.
 *
 * Design:
 *   - Endpoints are communication ports identified by capabilities
 *   - Messages can be register-only (64 bytes fast path) or buffered
 *   - Direct process switch when partner is waiting for low latency
 *   - Capabilities can be transferred with messages
 */

#ifndef _OCEAN_IPC_H
#define _OCEAN_IPC_H

#include <ocean/types.h>
#include <ocean/list.h>
#include <ocean/spinlock.h>

/* Forward declarations */
struct thread;
struct process;
struct capability;

/*
 * Message tag - packed into 64 bits
 *
 * [63:44] Label      - User-defined message type (20 bits)
 * [43:38] Length     - Number of data words (6 bits, 0-63)
 * [37:34] Cap count  - Capabilities transferred (4 bits, 0-15)
 * [33:26] Flags      - Operation flags (8 bits)
 * [25:10] Error      - Error code for replies (16 bits)
 * [9:0]   Reserved
 */
typedef u64 msg_tag_t;

#define MSG_TAG_LABEL_SHIFT     44
#define MSG_TAG_LABEL_MASK      0xFFFFF
#define MSG_TAG_LENGTH_SHIFT    38
#define MSG_TAG_LENGTH_MASK     0x3F
#define MSG_TAG_CAPS_SHIFT      34
#define MSG_TAG_CAPS_MASK       0xF
#define MSG_TAG_FLAGS_SHIFT     26
#define MSG_TAG_FLAGS_MASK      0xFF
#define MSG_TAG_ERROR_SHIFT     10
#define MSG_TAG_ERROR_MASK      0xFFFF

/* Message flags */
#define MSG_FLAG_GRANT          (1 << 0)    /* Grant (copy) capabilities */
#define MSG_FLAG_DONATE         (1 << 1)    /* Donate (move) capabilities */
#define MSG_FLAG_BLOCKING       (1 << 2)    /* Block if partner not ready */
#define MSG_FLAG_NONBLOCK       (1 << 3)    /* Return immediately if blocked */

/* Helper macros for message tags */
#define MSG_TAG(label, len, caps, flags) \
    (((u64)(label) << MSG_TAG_LABEL_SHIFT) | \
     ((u64)(len) << MSG_TAG_LENGTH_SHIFT) | \
     ((u64)(caps) << MSG_TAG_CAPS_SHIFT) | \
     ((u64)(flags) << MSG_TAG_FLAGS_SHIFT))

#define MSG_LABEL(tag)  (((tag) >> MSG_TAG_LABEL_SHIFT) & MSG_TAG_LABEL_MASK)
#define MSG_LENGTH(tag) (((tag) >> MSG_TAG_LENGTH_SHIFT) & MSG_TAG_LENGTH_MASK)
#define MSG_CAPS(tag)   (((tag) >> MSG_TAG_CAPS_SHIFT) & MSG_TAG_CAPS_MASK)
#define MSG_FLAGS(tag)  (((tag) >> MSG_TAG_FLAGS_SHIFT) & MSG_TAG_FLAGS_MASK)
#define MSG_ERROR(tag)  (((tag) >> MSG_TAG_ERROR_SHIFT) & MSG_TAG_ERROR_MASK)

#define MSG_SET_ERROR(tag, err) \
    (((tag) & ~((u64)MSG_TAG_ERROR_MASK << MSG_TAG_ERROR_SHIFT)) | \
     ((u64)(err) << MSG_TAG_ERROR_SHIFT))

/*
 * IPC Error codes
 */
#define IPC_OK              0       /* Success */
#define IPC_ERR_INVALID     1       /* Invalid endpoint/capability */
#define IPC_ERR_DEAD        2       /* Endpoint destroyed */
#define IPC_ERR_TIMEOUT     3       /* Operation timed out */
#define IPC_ERR_CANCELED    4       /* Operation canceled */
#define IPC_ERR_OVERFLOW    5       /* Message too large */
#define IPC_ERR_NOPARTNER   6       /* No partner waiting */
#define IPC_ERR_BUSY        7       /* Partner busy */
#define IPC_ERR_PERM        8       /* Permission denied */

/*
 * Fast path message buffer (register-only)
 * 64 bytes = 8 registers for zero-copy fast path
 */
#define IPC_FAST_REGS       8
#define IPC_FAST_SIZE       (IPC_FAST_REGS * sizeof(u64))

struct ipc_message {
    msg_tag_t tag;                      /* Message tag */
    u64 regs[IPC_FAST_REGS];           /* Register data (fast path) */

    /* Extended buffer for larger messages */
    void *buffer;                       /* Extended data buffer */
    size_t buffer_len;                  /* Buffer length */

    /* Capability transfer */
    u32 cap_slots[16];                  /* Capability slots to transfer */
    int nr_caps;                        /* Number of capabilities */
};

/*
 * IPC Endpoint - communication port
 *
 * An endpoint is a rendezvous point for IPC. Threads can send to or
 * receive from an endpoint. Access is controlled by capabilities.
 */
struct ipc_endpoint {
    u32 id;                             /* Endpoint ID */
    u32 flags;                          /* Endpoint flags */

    spinlock_t lock;                    /* Protects endpoint state */

    /* Wait queues for senders/receivers */
    struct list_head send_queue;        /* Threads waiting to send */
    struct list_head recv_queue;        /* Threads waiting to receive */

    /* Reference counting */
    int refcount;                       /* Reference count */

    /* Owner */
    struct process *owner;              /* Creating process */

    /* Bound thread (for reply endpoints) */
    struct thread *bound_thread;        /* Thread bound to this endpoint */

    /* Statistics */
    u64 msgs_sent;                      /* Messages sent through */
    u64 msgs_received;                  /* Messages received */

    /* List linkage */
    struct list_head list;              /* Global endpoint list */
};

/* Endpoint flags */
#define EP_FLAG_BOUND       (1 << 0)    /* Bound to specific thread */
#define EP_FLAG_REPLY       (1 << 1)    /* Reply endpoint */
#define EP_FLAG_NOTIFICATION (1 << 2)   /* Notification endpoint */
#define EP_FLAG_DEAD        (1 << 3)    /* Endpoint destroyed */

/*
 * IPC Wait state - saved when thread blocks on IPC
 */
struct ipc_wait {
    struct ipc_endpoint *endpoint;      /* Endpoint waiting on */
    struct ipc_message *msg;            /* Message being sent/received */
    struct thread *partner;             /* Partner thread (for direct switch) */
    int operation;                      /* IPC_OP_* */
    int result;                         /* Operation result */
    struct list_head wait_list;         /* Link in endpoint queue */
};

/* IPC operations */
#define IPC_OP_SEND         1
#define IPC_OP_RECV         2
#define IPC_OP_CALL         3           /* Send + receive (RPC) */
#define IPC_OP_REPLY        4
#define IPC_OP_REPLY_RECV   5           /* Reply + receive (server loop) */

/*
 * Thread IPC state - embedded in struct thread
 */
struct thread_ipc {
    struct ipc_wait wait;               /* Current IPC wait state */
    struct ipc_endpoint *reply_ep;      /* Reply endpoint for calls */
    struct ipc_message msg_buffer;      /* Message buffer */
};

/*
 * Capability - unforgeable token granting access to a resource
 */
#define CAP_TYPE_NONE       0           /* Empty slot */
#define CAP_TYPE_ENDPOINT   1           /* IPC endpoint */
#define CAP_TYPE_MEMORY     2           /* Memory region */
#define CAP_TYPE_THREAD     3           /* Thread control */
#define CAP_TYPE_PROCESS    4           /* Process control */
#define CAP_TYPE_IRQ        5           /* Interrupt */
#define CAP_TYPE_IO_PORT    6           /* I/O port range */
#define CAP_TYPE_NOTIFICATION 7         /* Notification object */

/* Capability rights */
#define CAP_RIGHT_READ      (1 << 0)
#define CAP_RIGHT_WRITE     (1 << 1)
#define CAP_RIGHT_GRANT     (1 << 2)    /* Can grant to others */
#define CAP_RIGHT_REVOKE    (1 << 3)    /* Can revoke */
#define CAP_RIGHT_SEND      (1 << 4)    /* Can send to endpoint */
#define CAP_RIGHT_RECV      (1 << 5)    /* Can receive from endpoint */

#define CAP_RIGHT_RW        (CAP_RIGHT_READ | CAP_RIGHT_WRITE)
#define CAP_RIGHT_ALL       0xFFFF

struct capability {
    u32 type;                           /* CAP_TYPE_* */
    u32 rights;                         /* CAP_RIGHT_* */
    u64 object;                         /* Pointer to object */
    u64 badge;                          /* User badge (for identification) */
    u32 generation;                     /* For revocation checking */
    u32 slot;                           /* Slot in capability space */
};

/*
 * Capability Space - per-process table of capabilities
 */
#define CSPACE_SIZE         256         /* Initial slots */
#define CSPACE_MAX_SIZE     65536       /* Maximum slots */

struct cspace {
    spinlock_t lock;
    struct capability *slots;           /* Array of capabilities */
    u32 size;                           /* Current size */
    u32 used;                           /* Used slots */
    u64 *bitmap;                        /* Free slot bitmap */
    u32 generation;                     /* For revocation */
};

/*
 * Notification object - async signaling
 */
struct notification {
    u32 id;
    spinlock_t lock;
    u64 word;                           /* Notification bits */
    struct list_head wait_queue;        /* Threads waiting */
    int refcount;
};

/*
 * IPC API
 */

/* Initialize IPC subsystem */
void ipc_init(void);

/* Endpoint operations */
struct ipc_endpoint *endpoint_create(struct process *owner, u32 flags);
void endpoint_destroy(struct ipc_endpoint *ep);
struct ipc_endpoint *endpoint_get(u32 id);
void endpoint_put(struct ipc_endpoint *ep);

/* Core IPC operations */
int ipc_send(struct ipc_endpoint *ep, struct ipc_message *msg);
int ipc_recv(struct ipc_endpoint *ep, struct ipc_message *msg);
int ipc_call(struct ipc_endpoint *ep, struct ipc_message *msg);
int ipc_reply(struct ipc_message *msg);
int ipc_reply_recv(struct ipc_endpoint *ep, struct ipc_message *msg);

/* Fast path (register-only, direct switch) */
int ipc_send_fast(u32 ep_cap, u64 tag, u64 *regs);
int ipc_recv_fast(u32 ep_cap, u64 *tag, u64 *regs);

/* Capability operations */
void cspace_init(struct cspace *cs);
void cspace_destroy(struct cspace *cs);
int cap_insert(struct cspace *cs, struct capability *cap);
struct capability *cap_lookup(struct cspace *cs, u32 slot);
int cap_delete(struct cspace *cs, u32 slot);
int cap_copy(struct cspace *dst, u32 dst_slot,
             struct cspace *src, u32 src_slot);
int cap_mint(struct cspace *dst, u32 dst_slot,
             struct cspace *src, u32 src_slot,
             u32 new_rights, u64 badge);

/* Notification operations */
struct notification *notification_create(void);
void notification_destroy(struct notification *ntfn);
int notification_signal(struct notification *ntfn, u64 bits);
int notification_wait(struct notification *ntfn, u64 *bits);
int notification_poll(struct notification *ntfn, u64 *bits);

/*
 * IPC syscalls (called from syscall dispatcher)
 */
i64 sys_ipc_send(u32 ep_cap, u64 tag, u64 r1, u64 r2, u64 r3, u64 r4);
i64 sys_ipc_recv(u32 ep_cap, u64 *tag, u64 *r1, u64 *r2, u64 *r3, u64 *r4);
i64 sys_ipc_call(u32 ep_cap, u64 tag, u64 r1, u64 r2, u64 r3, u64 r4);
i64 sys_ipc_reply(u64 tag, u64 r1, u64 r2, u64 r3, u64 r4);
i64 sys_ipc_reply_recv(u32 ep_cap, u64 tag, u64 r1, u64 r2, u64 r3, u64 r4);

i64 sys_endpoint_create(u32 flags);
i64 sys_cap_copy(u32 dst_slot, u32 src_slot);
i64 sys_cap_delete(u32 slot);

i64 sys_notify_signal(u32 ntfn_cap, u64 bits);
i64 sys_notify_wait(u32 ntfn_cap);
i64 sys_notify_poll(u32 ntfn_cap);

/*
 * Debug
 */
void ipc_dump_endpoint(struct ipc_endpoint *ep);
void ipc_dump_stats(void);

#endif /* _OCEAN_IPC_H */
