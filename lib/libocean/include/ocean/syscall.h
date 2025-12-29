/*
 * Ocean System Call Interface (Userspace)
 *
 * Defines system call numbers and provides inline syscall wrappers.
 */

#ifndef _OCEAN_USER_SYSCALL_H
#define _OCEAN_USER_SYSCALL_H

#include <stdint.h>

/*
 * System Call Numbers (must match kernel)
 */

/* Process control */
#define SYS_EXIT            0
#define SYS_FORK            1
#define SYS_EXEC            2
#define SYS_WAIT            3
#define SYS_GETPID          4
#define SYS_GETPPID         5

/* Thread control */
#define SYS_YIELD           10
#define SYS_SLEEP           11
#define SYS_THREAD_CREATE   12
#define SYS_THREAD_EXIT     13

/* Memory management */
#define SYS_BRK             20
#define SYS_MMAP            21
#define SYS_MUNMAP          22
#define SYS_MPROTECT        23

/* File operations */
#define SYS_OPEN            30
#define SYS_CLOSE           31
#define SYS_READ            32
#define SYS_WRITE           33
#define SYS_LSEEK           34

/* IPC - Message Passing */
#define SYS_IPC_SEND        50
#define SYS_IPC_RECV        51
#define SYS_IPC_CALL        52
#define SYS_IPC_REPLY       53
#define SYS_IPC_REPLY_RECV  54

/* IPC - Endpoints and Capabilities */
#define SYS_ENDPOINT_CREATE 60
#define SYS_ENDPOINT_DESTROY 61
#define SYS_CAP_COPY        62
#define SYS_CAP_DELETE      63
#define SYS_CAP_MINT        64
#define SYS_CAP_REVOKE      65

/* IPC - Notifications */
#define SYS_NOTIFY_SIGNAL   70
#define SYS_NOTIFY_WAIT     71
#define SYS_NOTIFY_POLL     72

/* Debugging */
#define SYS_DEBUG_PRINT     99

/*
 * Raw syscall wrappers
 *
 * These use inline assembly to invoke the SYSCALL instruction.
 * Arguments are passed in: RDI, RSI, RDX, R10, R8, R9
 * Syscall number is in RAX.
 * Return value is in RAX.
 */

static inline int64_t syscall0(int64_t nr)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall1(int64_t nr, int64_t a1)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall2(int64_t nr, int64_t a1, int64_t a2)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1), "S" (a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall3(int64_t nr, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1), "S" (a2), "d" (a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall4(int64_t nr, int64_t a1, int64_t a2,
                               int64_t a3, int64_t a4)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1), "S" (a2), "d" (a3), "r" (r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall5(int64_t nr, int64_t a1, int64_t a2,
                               int64_t a3, int64_t a4, int64_t a5)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8 __asm__("r8") = a5;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1), "S" (a2), "d" (a3), "r" (r10), "r" (r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall6(int64_t nr, int64_t a1, int64_t a2,
                               int64_t a3, int64_t a4, int64_t a5, int64_t a6)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8 __asm__("r8") = a5;
    register int64_t r9 __asm__("r9") = a6;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (nr), "D" (a1), "S" (a2), "d" (a3), "r" (r10), "r" (r8), "r" (r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/*
 * Typed syscall wrappers
 */

static inline void _exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static inline int getpid(void)
{
    return (int)syscall0(SYS_GETPID);
}

static inline int getppid(void)
{
    return (int)syscall0(SYS_GETPPID);
}

static inline int yield(void)
{
    return (int)syscall0(SYS_YIELD);
}

static inline int64_t write(int fd, const void *buf, uint64_t count)
{
    return syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

static inline int64_t read(int fd, void *buf, uint64_t count)
{
    return syscall3(SYS_READ, fd, (int64_t)buf, count);
}

static inline int debug_print(const char *msg, uint64_t len)
{
    return (int)syscall2(SYS_DEBUG_PRINT, (int64_t)msg, len);
}

/*
 * IPC syscalls
 */

static inline int endpoint_create(uint32_t flags)
{
    return (int)syscall1(SYS_ENDPOINT_CREATE, flags);
}

static inline int endpoint_destroy(uint32_t ep_id)
{
    return (int)syscall1(SYS_ENDPOINT_DESTROY, ep_id);
}

static inline int64_t ipc_send(uint32_t ep, uint64_t tag,
                               uint64_t r1, uint64_t r2, uint64_t r3, uint64_t r4)
{
    return syscall6(SYS_IPC_SEND, ep, tag, r1, r2, r3, r4);
}

static inline int64_t ipc_recv(uint32_t ep, uint64_t *tag,
                               uint64_t *r1, uint64_t *r2, uint64_t *r3, uint64_t *r4)
{
    return syscall6(SYS_IPC_RECV, ep, (int64_t)tag,
                    (int64_t)r1, (int64_t)r2, (int64_t)r3, (int64_t)r4);
}

#endif /* _OCEAN_USER_SYSCALL_H */
