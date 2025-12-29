/*
 * Ocean Kernel - System Call Interface
 *
 * Defines system call numbers and related structures.
 */

#ifndef _OCEAN_SYSCALL_H
#define _OCEAN_SYSCALL_H

#include <ocean/types.h>

/*
 * System Call Numbers
 *
 * We follow a Linux-like numbering scheme for familiarity,
 * but with a smaller initial set.
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

/* File operations (basic) */
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

/* Debugging/testing */
#define SYS_DEBUG_PRINT     99

/* Maximum syscall number */
#define NR_SYSCALLS         128

/*
 * Syscall argument passing (x86_64 System V ABI):
 *
 * Syscall number in RAX
 * Arguments: RDI, RSI, RDX, R10, R8, R9 (up to 6 args)
 * Return value in RAX
 *
 * Note: RCX and R11 are clobbered by SYSCALL instruction
 * (RCX gets RIP, R11 gets RFLAGS)
 */

/*
 * Syscall return structure
 * For syscalls that need to return multiple values
 */
struct syscall_result {
    i64 value;      /* Primary return value */
    i64 error;      /* Error code (0 = success) */
};

/*
 * Syscall handler function type
 */
typedef i64 (*syscall_handler_t)(u64 arg1, u64 arg2, u64 arg3,
                                  u64 arg4, u64 arg5, u64 arg6);

/*
 * Syscall initialization
 */
void syscall_init(void);

/*
 * Syscall dispatcher (called from assembly)
 */
i64 syscall_dispatch(u64 nr, u64 arg1, u64 arg2, u64 arg3,
                     u64 arg4, u64 arg5, u64 arg6);

/*
 * MSRs for SYSCALL/SYSRET
 */
#define MSR_EFER            0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR            0xC0000081  /* Syscall segment selectors */
#define MSR_LSTAR           0xC0000082  /* Syscall entry (64-bit) */
#define MSR_CSTAR           0xC0000083  /* Syscall entry (compat mode) */
#define MSR_SFMASK          0xC0000084  /* Syscall RFLAGS mask */

/* EFER bits */
#define EFER_SCE            (1 << 0)    /* Syscall enable */
#define EFER_LME            (1 << 8)    /* Long mode enable */
#define EFER_LMA            (1 << 10)   /* Long mode active */
#define EFER_NXE            (1 << 11)   /* No-execute enable */

/* RFLAGS bits to clear on SYSCALL */
#define SYSCALL_RFLAGS_MASK (0x200 | 0x100 | 0x40000)  /* IF, TF, AC */

#endif /* _OCEAN_SYSCALL_H */
