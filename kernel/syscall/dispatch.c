/*
 * Ocean Kernel - System Call Dispatcher
 *
 * Routes system calls to their handlers and manages the syscall table.
 */

#include <ocean/syscall.h>
#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/ipc.h>
#include <ocean/uaccess.h>
#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/boot.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern char *strstr(const char *haystack, const char *needle);

/* Assembly entry point */
extern void syscall_entry_simple(void);

/* Per-CPU data for syscall handling */
struct percpu_syscall {
    u64 user_rsp;           /* Saved user RSP (offset 0) */
    u64 kernel_rsp;         /* Thread kernel stack top (offset 8) */
    u64 scratch;            /* Scratch space (offset 16) */
    u64 trampoline_rsp;     /* Boot/fallback stack (offset 24) */
};

/* For now, single CPU only */
static struct percpu_syscall percpu_data __aligned(16);

/*
 * Get the per-CPU kernel RSP (used by fork to copy syscall frame)
 */
u64 get_percpu_kernel_rsp(void)
{
    return percpu_data.kernel_rsp;
}

/*
 * Set the per-CPU kernel RSP to a thread's kernel stack top
 * Called during context switch to update which stack syscalls use
 */
void set_percpu_kernel_rsp(u64 rsp)
{
    percpu_data.kernel_rsp = rsp;
}

/*
 * System call handlers
 */

/* SYS_EXIT - Terminate the current process */
static i64 sys_exit(i64 code)
{
    process_exit((int)code);
    /* Never returns */
    return 0;
}

/* SYS_GETPID - Get process ID */
static i64 sys_getpid(void)
{
    struct process *proc = get_current_process();
    return proc ? proc->pid : -1;
}

/* SYS_GETPPID - Get parent process ID */
static i64 sys_getppid(void)
{
    struct process *proc = get_current_process();
    return proc ? proc->ppid : -1;
}

/* SYS_YIELD - Yield the CPU */
static i64 sys_yield(void)
{
    sched_yield();
    return 0;
}

/* SYS_DEBUG_PRINT - Debug print (for testing) */
static i64 sys_debug_print(const char *msg, u64 len)
{
    if (len == 0) {
        return 0;
    }
    if (!msg) {
        return -EFAULT;
    }

    extern void serial_putc(char c);

    char chunk[128];
    u64 total = 0;
    while (total < len) {
        size_t n = (size_t)MIN((u64)sizeof(chunk), len - total);
        int ret = copy_from_user(chunk, msg + total, n);
        if (ret < 0) {
            return (total > 0) ? (i64)total : ret;
        }

        for (size_t i = 0; i < n; i++) {
            serial_putc(chunk[i]);
        }

        total += n;
    }

    return (i64)total;
}

/* SYS_READ - Read from file descriptor (minimal implementation) */
static i64 sys_read(int fd, char *buf, u64 count)
{
    /* For now, only support stdin (fd 0) */
    if (fd != 0) {
        return -EBADF;
    }
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    extern int serial_getc(void);
    extern void serial_putc(char c);
    extern bool serial_data_available(void);

    u64 i = 0;
    while (i < count) {
        /* Wait for data with interrupts enabled so timer can tick */
        while (!serial_data_available()) {
            __asm__ volatile("sti; hlt; cli");  /* Enable, halt, disable */
        }

        /* Read with interrupts disabled */
        __asm__ volatile("cli");
        int c = serial_getc();
        __asm__ volatile("sti");

        if (c < 0) {
            break;
        }

        char out = (char)c;

        /* Echo the character */
        serial_putc(out);

        /* Stop at newline */
        if (c == '\n' || c == '\r') {
            if (c == '\r') {
                out = '\n';
                serial_putc('\n');
            }
            int ret = copy_to_user(buf + i, &out, 1);
            if (ret < 0) {
                return (i > 0) ? (i64)i : ret;
            }
            i++;
            break;
        }

        int ret = copy_to_user(buf + i, &out, 1);
        if (ret < 0) {
            return (i > 0) ? (i64)i : ret;
        }
        i++;
    }

    return (i64)i;
}

/* SYS_WRITE - Write to file descriptor (minimal implementation) */
static i64 sys_write(int fd, const char *buf, u64 count)
{
    /* For now, only support stdout (fd 1) and stderr (fd 2) */
    if (fd != 1 && fd != 2) {
        return -EBADF;
    }
    if (count == 0) {
        return 0;
    }
    if (!buf) {
        return -EFAULT;
    }

    extern void serial_putc(char c);

    char chunk[128];
    u64 total = 0;
    while (total < count) {
        size_t n = (size_t)MIN((u64)sizeof(chunk), count - total);
        int ret = copy_from_user(chunk, buf + total, n);
        if (ret < 0) {
            return (total > 0) ? (i64)total : ret;
        }

        for (size_t i = 0; i < n; i++) {
            serial_putc(chunk[i]);
        }

        total += n;
    }

    return (i64)total;
}

/*
 * Process creation syscalls
 */

/* SYS_FORK - Create child process */
static i64 sys_fork(void)
{
    return (i64)process_fork();
}

/* Find a boot module by name (searches cmdline for the name) */
static struct cached_module *find_boot_module(const char *name)
{
    const struct boot_info *boot = get_boot_info();

    for (u64 i = 0; i < boot->cached_module_count; i++) {
        struct cached_module *mod = (struct cached_module *)&boot->cached_modules[i];
        /* Check if name appears in the module's cmdline/path */
        if (strstr(mod->cmdline, name) != NULL) {
            return mod;
        }
    }
    return NULL;
}

/* SYS_EXEC - Execute a program (replaces current process) */
static i64 sys_exec(const char *path, char *const argv[], char *const envp[])
{
    (void)argv;
    (void)envp;

    if (!path) {
        return -EINVAL;
    }

    char kpath[256];
    int path_len = copy_string_from_user(kpath, sizeof(kpath), path);
    if (path_len < 0) {
        return path_len;
    }

    /* Find the module in boot modules */
    struct cached_module *mod = find_boot_module(kpath);
    if (!mod) {
        kprintf("exec: '%s' not found\n", kpath);
        return -ENOENT;
    }

    /* Extract just the filename from path */
    const char *name = kpath;
    const char *p = kpath;
    while (*p) {
        if (*p == '/') {
            name = p + 1;
        }
        p++;
    }

    /* Load ELF and replace current process */
    extern int exec_replace(const void *elf_data, size_t elf_size, const char *name);
    exec_replace(mod->address, mod->size, name);

    /* exec_replace never returns on success */
    return -EIO;
}

/* SYS_WAIT - Wait for child process */
static i64 sys_wait(int *status)
{
    int kstatus = 0;
    pid_t pid = process_wait(status ? &kstatus : NULL);
    if (pid < 0) {
        return (i64)pid;
    }

    if (status) {
        int ret = copy_to_user(status, &kstatus, sizeof(kstatus));
        if (ret < 0) {
            return ret;
        }
    }

    return (i64)pid;
}

/*
 * IPC System Call Handlers
 */

/* SYS_IPC_SEND - Send a message to an endpoint */
static i64 sys_ipc_send_impl(u32 ep_cap, u64 tag, u64 r1, u64 r2, u64 r3, u64 r4)
{
    u64 regs[IPC_FAST_REGS] = {r1, r2, r3, r4, 0, 0, 0, 0};
    int result = ipc_send_fast(ep_cap, tag, regs);
    return (i64)result;
}

/* SYS_IPC_RECV - Receive a message from an endpoint */
static i64 sys_ipc_recv_impl(u32 ep_cap, u64 tag_ptr, u64 r1_ptr, u64 r2_ptr, u64 r3_ptr, u64 r4_ptr)
{
    u64 tag = 0;
    u64 regs[IPC_FAST_REGS] = {0};

    int result = ipc_recv_fast(ep_cap, &tag, regs);

    /* Copy results back to user pointers */
    if (result == IPC_OK && tag_ptr) {
        int ret = copy_to_user((void *)tag_ptr, &tag, sizeof(tag));
        if (ret < 0) {
            return ret;
        }
        if (r1_ptr) {
            ret = copy_to_user((void *)r1_ptr, &regs[0], sizeof(regs[0]));
            if (ret < 0) return ret;
        }
        if (r2_ptr) {
            ret = copy_to_user((void *)r2_ptr, &regs[1], sizeof(regs[1]));
            if (ret < 0) return ret;
        }
        if (r3_ptr) {
            ret = copy_to_user((void *)r3_ptr, &regs[2], sizeof(regs[2]));
            if (ret < 0) return ret;
        }
        if (r4_ptr) {
            ret = copy_to_user((void *)r4_ptr, &regs[3], sizeof(regs[3]));
            if (ret < 0) return ret;
        }
    }

    return (i64)result;
}

/* SYS_ENDPOINT_CREATE - Create a new endpoint */
static i64 sys_endpoint_create_impl(u32 flags)
{
    struct process *proc = get_current_process();
    if (!proc) {
        return -IPC_ERR_INVALID;
    }

    struct ipc_endpoint *ep = endpoint_create(proc, flags);
    if (!ep) {
        return -IPC_ERR_INVALID;
    }

    return (i64)ep->id;
}

/* SYS_ENDPOINT_DESTROY - Destroy an endpoint */
static i64 sys_endpoint_destroy_impl(u32 ep_id)
{
    struct ipc_endpoint *ep = endpoint_get(ep_id);
    if (!ep) {
        return -IPC_ERR_INVALID;
    }

    endpoint_destroy(ep);
    endpoint_put(ep);
    return 0;
}

/*
 * System call table
 */
static syscall_handler_t syscall_table[NR_SYSCALLS] = {
    /* Process control */
    [SYS_EXIT]          = (syscall_handler_t)sys_exit,
    [SYS_FORK]          = (syscall_handler_t)sys_fork,
    [SYS_EXEC]          = (syscall_handler_t)sys_exec,
    [SYS_WAIT]          = (syscall_handler_t)sys_wait,
    [SYS_GETPID]        = (syscall_handler_t)sys_getpid,
    [SYS_GETPPID]       = (syscall_handler_t)sys_getppid,

    /* Thread control */
    [SYS_YIELD]         = (syscall_handler_t)sys_yield,

    /* File operations */
    [SYS_READ]          = (syscall_handler_t)sys_read,
    [SYS_WRITE]         = (syscall_handler_t)sys_write,

    /* IPC - Message passing */
    [SYS_IPC_SEND]      = (syscall_handler_t)sys_ipc_send_impl,
    [SYS_IPC_RECV]      = (syscall_handler_t)sys_ipc_recv_impl,

    /* IPC - Endpoints */
    [SYS_ENDPOINT_CREATE] = (syscall_handler_t)sys_endpoint_create_impl,
    [SYS_ENDPOINT_DESTROY] = (syscall_handler_t)sys_endpoint_destroy_impl,

    /* Debug */
    [SYS_DEBUG_PRINT]   = (syscall_handler_t)sys_debug_print,
};

/*
 * Syscall dispatcher - called from assembly with register frame
 *
 * frame points to saved registers on kernel stack:
 *   r15, r14, r13, r12, r11, r10, r9, r8,
 *   rbp, rdi, rsi, rdx, rcx, rbx, rax,
 *   int_no, error_code,
 *   rip, cs, rflags, rsp, ss
 */
i64 syscall_dispatch_frame(u64 nr, u64 *frame)
{
    /* Extract arguments from saved frame */
    /* Frame layout (from entry.asm, bottom to top of stack):
     *   [0] = r15, [1] = r14, ..., [6] = r9, [7] = r8
     *   [8] = rbp, [9] = rdi, [10] = rsi, [11] = rdx
     *   [12] = rcx, [13] = rbx, [14] = rax (syscall nr)
     */
    u64 arg1 = frame[9];   /* rdi */
    u64 arg2 = frame[10];  /* rsi */
    u64 arg3 = frame[11];  /* rdx */
    u64 arg4 = frame[6];   /* r10 -> use r9 slot since r10 was moved? */
                           /* Actually r10 is at [5] */
    u64 arg5 = frame[7];   /* r8 */
    u64 arg6 = frame[6];   /* r9 */

    /* Fix: r10 is at index 5 (counting from r15=0) */
    arg4 = frame[5];       /* r10 */
    arg6 = frame[6];       /* r9 */

    return syscall_dispatch(nr, arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
 * Main syscall dispatcher
 */
i64 syscall_dispatch(u64 nr, u64 arg1, u64 arg2, u64 arg3,
                     u64 arg4, u64 arg5, u64 arg6)
{
    /* Validate syscall number */
    if (nr >= NR_SYSCALLS) {
        kprintf("[syscall] Invalid syscall number: %llu\n", nr);
        return -ENOSYS;
    }

    /* Get handler */
    syscall_handler_t handler = syscall_table[nr];
    if (!handler) {
        kprintf("[syscall] Unimplemented syscall: %llu\n", nr);
        return -ENOSYS;
    }

    /* Call handler */
    return handler(arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
 * Initialize system call handling
 */
void syscall_init(void)
{
    kprintf("Initializing system calls...\n");

    /* Set up per-CPU data */
    memset(&percpu_data, 0, sizeof(percpu_data));

    /* Allocate a trampoline stack for boot/fallback */
    extern void *kmalloc(size_t size);
    void *syscall_stack = kmalloc(8192);
    if (!syscall_stack) {
        kprintf("  Failed to allocate syscall stack!\n");
        return;
    }
    u64 stack_top = (u64)syscall_stack + 8192 - 8;
    percpu_data.trampoline_rsp = stack_top;
    percpu_data.kernel_rsp = stack_top;  /* Use trampoline until threads run */

    kprintf("  Trampoline stack at %p\n", syscall_stack);

    /* Set up GS base to point to per-CPU data */
    /* MSR_GS_BASE = 0xC0000101, MSR_KERNEL_GS_BASE = 0xC0000102 */
    #define MSR_GS_BASE         0xC0000101
    #define MSR_KERNEL_GS_BASE  0xC0000102

    /* Set kernel GS base (swapped in by SWAPGS) */
    wrmsr(MSR_KERNEL_GS_BASE, (u64)&percpu_data);
    kprintf("  Kernel GS base set to %p\n", &percpu_data);

    /* Enable SYSCALL/SYSRET in EFER */
    u64 efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;  /* Syscall enable */
    wrmsr(MSR_EFER, efer);
    kprintf("  EFER.SCE enabled\n");

    /*
     * Configure STAR MSR
     *
     * STAR layout:
     *   [31:0]  = Reserved (or SYSCALL EIP for 32-bit, unused in 64-bit)
     *   [47:32] = SYSCALL CS/SS base (kernel segments)
     *   [63:48] = SYSRET CS/SS base (user segments)
     *
     * For SYSCALL: CS = STAR[47:32], SS = STAR[47:32] + 8
     * For SYSRET:  CS = STAR[63:48] + 16 (64-bit), SS = STAR[63:48] + 8
     *
     * Our GDT:
     *   0x08 = Kernel CS
     *   0x10 = Kernel DS/SS
     *   0x18 = User CS32
     *   0x20 = User DS/SS
     *   0x28 = User CS64
     *
     * So: SYSCALL base = 0x08 (kernel CS at 0x08, SS at 0x10)
     *     SYSRET base  = 0x18 (SS at 0x18+8=0x20, CS64 at 0x18+16=0x28)
     *
     * Wait, SYSRET CS = base + 16 for 64-bit, base + 0 for 32-bit
     * SYSRET SS = base + 8
     *
     * We want User CS = 0x28, User SS = 0x20
     * So base = 0x28 - 16 = 0x18
     */
    u64 star = ((u64)0x18 << 48) |  /* SYSRET CS/SS base */
               ((u64)0x08 << 32);    /* SYSCALL CS/SS base */
    wrmsr(MSR_STAR, star);
    kprintf("  STAR MSR configured\n");

    /* Set LSTAR to our syscall entry point */
    wrmsr(MSR_LSTAR, (u64)syscall_entry_simple);
    kprintf("  LSTAR set to syscall_entry_simple\n");

    /* CSTAR is for 32-bit compatibility mode - not used yet */
    wrmsr(MSR_CSTAR, 0);

    /* Set SFMASK - flags to clear on SYSCALL */
    /* Clear IF (interrupts), TF (trap flag), AC (alignment check) */
    wrmsr(MSR_SFMASK, SYSCALL_RFLAGS_MASK);
    kprintf("  SFMASK configured\n");

    kprintf("System calls initialized\n");
}
