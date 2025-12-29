/*
 * Ocean Kernel - Common Definitions and Macros
 */

#ifndef _OCEAN_DEFS_H
#define _OCEAN_DEFS_H

#include <ocean/types.h>

/*
 * Compiler attributes
 */

/* Function doesn't return */
#define __noreturn          __attribute__((noreturn))

/* Function is always inlined */
#define __always_inline     inline __attribute__((always_inline))

/* Variable/function is unused */
#define __unused            __attribute__((unused))

/* Struct is packed (no padding) */
#define __packed            __attribute__((packed))

/* Alignment */
#define __aligned(n)        __attribute__((aligned(n)))

/* Section placement */
#define __section(s)        __attribute__((section(s)))

/* Likely/unlikely branch hints */
#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

/* Prevent compiler reordering */
#define barrier()           __asm__ __volatile__("" ::: "memory")

/* Read/write memory barriers for SMP */
#define mb()                __asm__ __volatile__("mfence" ::: "memory")
#define rmb()               __asm__ __volatile__("lfence" ::: "memory")
#define wmb()               __asm__ __volatile__("sfence" ::: "memory")

/* Printf format checking */
#define __printf(fmt, args) __attribute__((format(printf, fmt, args)))

/* Warn if return value is ignored */
#define __must_check        __attribute__((warn_unused_result))

/*
 * Memory and size constants
 */

/* Page sizes */
#define PAGE_SHIFT          12
#define PAGE_SIZE           (1UL << PAGE_SHIFT)         /* 4 KiB */
#define PAGE_MASK           (~(PAGE_SIZE - 1))

#define LARGE_PAGE_SHIFT    21
#define LARGE_PAGE_SIZE     (1UL << LARGE_PAGE_SHIFT)   /* 2 MiB */

#define HUGE_PAGE_SHIFT     30
#define HUGE_PAGE_SIZE      (1UL << HUGE_PAGE_SHIFT)    /* 1 GiB */

/* Kernel virtual addresses */
#define KERNEL_VMA          0xFFFFFFFF80000000UL
#define KERNEL_PHYS_BASE    0xFFFF800000000000UL

/* Convert between physical and kernel virtual addresses */
#define PHYS_TO_VIRT(p)     ((void *)((uintptr_t)(p) + KERNEL_PHYS_BASE))
#define VIRT_TO_PHYS(v)     ((phys_addr_t)((uintptr_t)(v) - KERNEL_PHYS_BASE))

/*
 * Alignment macros
 */

/* Align up to boundary */
#define ALIGN_UP(x, a)      (((x) + ((a) - 1)) & ~((a) - 1))

/* Align down to boundary */
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))

/* Check if aligned */
#define IS_ALIGNED(x, a)    (((x) & ((a) - 1)) == 0)

/* Page alignment */
#define PAGE_ALIGN(x)       ALIGN_UP(x, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(x)  ALIGN_DOWN(x, PAGE_SIZE)

/*
 * Utility macros
 */

/* Get number of elements in array */
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))

/* Get minimum/maximum */
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

/* Clamp value to range */
#define CLAMP(x, lo, hi)    MIN(MAX(x, lo), hi)

/* Bit manipulation */
#define BIT(n)              (1UL << (n))
#define BITS(hi, lo)        ((BIT((hi) - (lo) + 1) - 1) << (lo))
#define SET_BIT(x, n)       ((x) |= BIT(n))
#define CLEAR_BIT(x, n)     ((x) &= ~BIT(n))
#define TEST_BIT(x, n)      (((x) & BIT(n)) != 0)

/* Get offset of member in struct */
#define offsetof(type, member)  __builtin_offsetof(type, member)

/* Get container struct from member pointer */
#define container_of(ptr, type, member) ({                      \
    const typeof(((type *)0)->member) *__mptr = (ptr);          \
    (type *)((char *)__mptr - offsetof(type, member));          \
})

/*
 * Error codes (POSIX-compatible)
 */
#define EPERM           1       /* Operation not permitted */
#define ENOENT          2       /* No such file or directory */
#define ESRCH           3       /* No such process */
#define EINTR           4       /* Interrupted system call */
#define EIO             5       /* I/O error */
#define ENXIO           6       /* No such device or address */
#define E2BIG           7       /* Argument list too long */
#define ENOEXEC         8       /* Exec format error */
#define EBADF           9       /* Bad file number */
#define ECHILD          10      /* No child processes */
#define EAGAIN          11      /* Try again */
#define ENOMEM          12      /* Out of memory */
#define EACCES          13      /* Permission denied */
#define EFAULT          14      /* Bad address */
#define EBUSY           16      /* Device or resource busy */
#define EEXIST          17      /* File exists */
#define EXDEV           18      /* Cross-device link */
#define ENODEV          19      /* No such device */
#define ENOTDIR         20      /* Not a directory */
#define EISDIR          21      /* Is a directory */
#define EINVAL          22      /* Invalid argument */
#define ENFILE          23      /* File table overflow */
#define EMFILE          24      /* Too many open files */
#define ENOTTY          25      /* Not a typewriter */
#define EFBIG           27      /* File too large */
#define ENOSPC          28      /* No space left on device */
#define ESPIPE          29      /* Illegal seek */
#define EROFS           30      /* Read-only file system */
#define EMLINK          31      /* Too many links */
#define EPIPE           32      /* Broken pipe */
#define EDOM            33      /* Math argument out of domain */
#define ERANGE          34      /* Math result not representable */
#define EDEADLK         35      /* Resource deadlock would occur */
#define ENAMETOOLONG    36      /* File name too long */
#define ENOLCK          37      /* No record locks available */
#define ENOSYS          38      /* Function not implemented */
#define ENOTEMPTY       39      /* Directory not empty */
#define ELOOP           40      /* Too many symbolic links */
#define EWOULDBLOCK     EAGAIN  /* Operation would block */
#define ENOTSUP         95      /* Operation not supported */
#define ETIMEDOUT       110     /* Connection timed out */

/* Error pointer helpers */
#define MAX_ERRNO       4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#define ERR_PTR(e)      ((void *)(long)(e))
#define PTR_ERR(p)      ((long)(p))
#define IS_ERR(p)       IS_ERR_VALUE((unsigned long)(p))

/*
 * Assertions
 */

/* Static assertion (compile-time) */
#define static_assert(expr, msg) _Static_assert(expr, msg)

/* Kernel panic declaration */
void panic(const char *fmt, ...) __noreturn __printf(1, 2);

/* Runtime assertion */
#define ASSERT(expr) do {                                               \
    if (unlikely(!(expr))) {                                            \
        panic("Assertion failed: %s\nFile: %s, Line: %d",               \
              #expr, __FILE__, __LINE__);                               \
    }                                                                   \
} while (0)

/* Debug assertion (can be disabled) */
#ifdef OCEAN_DEBUG
#define DEBUG_ASSERT(expr) ASSERT(expr)
#else
#define DEBUG_ASSERT(expr) ((void)0)
#endif

/*
 * CPU-specific inline assembly helpers
 */

/* Halt CPU */
static __always_inline void halt(void)
{
    __asm__ __volatile__("hlt");
}

/* Halt with interrupts disabled */
static __always_inline __noreturn void halt_forever(void)
{
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

/* Disable interrupts */
static __always_inline void cli(void)
{
    __asm__ __volatile__("cli" ::: "memory");
}

/* Enable interrupts */
static __always_inline void sti(void)
{
    __asm__ __volatile__("sti" ::: "memory");
}

/* Read FLAGS register */
static __always_inline u64 read_flags(void)
{
    u64 flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return flags;
}

/* Save flags and disable interrupts */
static __always_inline u64 local_irq_save(void)
{
    u64 flags = read_flags();
    cli();
    return flags;
}

/* Restore flags */
static __always_inline void local_irq_restore(u64 flags)
{
    if (flags & 0x200) {  /* IF flag */
        sti();
    }
}

/* I/O port operations */
static __always_inline void outb(u16 port, u8 val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline void outw(u16 port, u16 val)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline void outl(u16 port, u32 val)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static __always_inline u8 inb(u16 port)
{
    u8 val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static __always_inline u16 inw(u16 port)
{
    u16 val;
    __asm__ __volatile__("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static __always_inline u32 inl(u16 port)
{
    u32 val;
    __asm__ __volatile__("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* I/O wait (small delay) */
static __always_inline void io_wait(void)
{
    outb(0x80, 0);
}

/* Read CPU timestamp counter */
static __always_inline u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* Read model-specific register */
static __always_inline u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

/* Write model-specific register */
static __always_inline void wrmsr(u32 msr, u64 value)
{
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

/* Read CR0 */
static __always_inline u64 read_cr0(void)
{
    u64 val;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(val));
    return val;
}

/* Write CR0 */
static __always_inline void write_cr0(u64 val)
{
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(val));
}

/* Read CR2 (page fault address) */
static __always_inline u64 read_cr2(void)
{
    u64 val;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(val));
    return val;
}

/* Read CR3 (page table base) */
static __always_inline u64 read_cr3(void)
{
    u64 val;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
    return val;
}

/* Write CR3 (switch page tables) */
static __always_inline void write_cr3(u64 val)
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(val) : "memory");
}

/* Read CR4 */
static __always_inline u64 read_cr4(void)
{
    u64 val;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(val));
    return val;
}

/* Write CR4 */
static __always_inline void write_cr4(u64 val)
{
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(val));
}

/* Invalidate TLB entry */
static __always_inline void invlpg(uintptr_t addr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(addr) : "memory");
}

/* CPU pause (for spinlock loops) */
static __always_inline void cpu_pause(void)
{
    __asm__ __volatile__("pause");
}

#endif /* _OCEAN_DEFS_H */
