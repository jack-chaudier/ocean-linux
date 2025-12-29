/*
 * Ocean Kernel - Global Descriptor Table (GDT)
 *
 * x86_64 GDT setup with TSS for each CPU.
 * In long mode, the GDT is mostly vestigial but still required for:
 *   - Defining code/data segments (though they're flat)
 *   - TSS for stack switching on privilege transitions
 *   - syscall/sysret segment selectors
 */

#ifndef _OCEAN_GDT_H
#define _OCEAN_GDT_H

#include <ocean/types.h>
#include <ocean/defs.h>

/*
 * Segment selectors
 *
 * In x86_64, we use a flat memory model. Segments are still required
 * but base/limit are ignored for code/data segments.
 *
 * Layout:
 *   0x00 - Null descriptor (required)
 *   0x08 - Kernel code (64-bit)
 *   0x10 - Kernel data
 *   0x18 - User code (32-bit, for compatibility mode)
 *   0x20 - User data
 *   0x28 - User code (64-bit)
 *   0x30 - TSS (16 bytes, takes two slots)
 */

#define GDT_NULL            0x00
#define GDT_KERNEL_CODE     0x08
#define GDT_KERNEL_DATA     0x10
#define GDT_USER_CODE32     0x18
#define GDT_USER_DATA       0x20
#define GDT_USER_CODE64     0x28
#define GDT_TSS             0x30

/* Segment selector with RPL (Requested Privilege Level) */
#define KERNEL_CS           (GDT_KERNEL_CODE | 0)
#define KERNEL_DS           (GDT_KERNEL_DATA | 0)
#define USER_CS32           (GDT_USER_CODE32 | 3)
#define USER_DS             (GDT_USER_DATA | 3)
#define USER_CS             (GDT_USER_CODE64 | 3)
#define TSS_SELECTOR        (GDT_TSS | 0)

/* Number of GDT entries (including TSS which takes 2 slots) */
#define GDT_ENTRIES         7

/*
 * GDT Entry (8 bytes)
 *
 * For code/data segments in long mode, most fields are ignored.
 * Only the following bits matter:
 *   - Type (code/data, read/write, etc.)
 *   - S (descriptor type: 0=system, 1=code/data)
 *   - DPL (privilege level)
 *   - P (present)
 *   - L (long mode - 64-bit code segment)
 *   - D/B (default operation size - must be 0 for 64-bit)
 */
struct gdt_entry {
    u16 limit_low;          /* Limit bits 0-15 */
    u16 base_low;           /* Base bits 0-15 */
    u8  base_middle;        /* Base bits 16-23 */
    u8  access;             /* Access byte */
    u8  granularity;        /* Granularity + limit bits 16-19 */
    u8  base_high;          /* Base bits 24-31 */
} __packed;

/*
 * TSS Entry (16 bytes) - System segment descriptor
 *
 * In long mode, system descriptors are 16 bytes.
 */
struct tss_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
    u32 base_upper;         /* Upper 32 bits of base (64-bit) */
    u32 reserved;
} __packed;

/*
 * GDT Pointer (for LGDT instruction)
 */
struct gdt_ptr {
    u16 limit;              /* Size of GDT - 1 */
    u64 base;               /* Linear address of GDT */
} __packed;

/*
 * Task State Segment (TSS)
 *
 * In long mode, TSS is used for:
 *   - RSP0-2: Stack pointers for privilege level switches
 *   - IST1-7: Interrupt Stack Table for specific interrupts
 *   - IOPB: I/O permission bitmap base
 */
struct tss {
    u32 reserved0;
    u64 rsp0;               /* Stack pointer for ring 0 */
    u64 rsp1;               /* Stack pointer for ring 1 (unused) */
    u64 rsp2;               /* Stack pointer for ring 2 (unused) */
    u64 reserved1;
    u64 ist1;               /* Interrupt Stack Table 1 */
    u64 ist2;               /* IST 2 */
    u64 ist3;               /* IST 3 */
    u64 ist4;               /* IST 4 */
    u64 ist5;               /* IST 5 */
    u64 ist6;               /* IST 6 */
    u64 ist7;               /* IST 7 */
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;        /* I/O permission bitmap offset */
} __packed;

/* TSS size */
#define TSS_SIZE            sizeof(struct tss)

/* Access byte flags */
#define GDT_ACCESS_PRESENT  (1 << 7)    /* Segment present */
#define GDT_ACCESS_DPL0     (0 << 5)    /* Ring 0 */
#define GDT_ACCESS_DPL1     (1 << 5)    /* Ring 1 */
#define GDT_ACCESS_DPL2     (2 << 5)    /* Ring 2 */
#define GDT_ACCESS_DPL3     (3 << 5)    /* Ring 3 */
#define GDT_ACCESS_SEGMENT  (1 << 4)    /* Code/data segment (not system) */
#define GDT_ACCESS_EXEC     (1 << 3)    /* Executable (code segment) */
#define GDT_ACCESS_DC       (1 << 2)    /* Direction/Conforming */
#define GDT_ACCESS_RW       (1 << 1)    /* Readable (code) / Writable (data) */
#define GDT_ACCESS_ACCESSED (1 << 0)    /* Accessed */

/* System segment types */
#define GDT_TYPE_TSS_AVAIL  0x9         /* 64-bit TSS (Available) */
#define GDT_TYPE_TSS_BUSY   0xB         /* 64-bit TSS (Busy) */

/* Granularity byte flags */
#define GDT_GRAN_4K         (1 << 7)    /* 4K granularity */
#define GDT_GRAN_32BIT      (1 << 6)    /* 32-bit default operand size */
#define GDT_GRAN_64BIT      (1 << 5)    /* 64-bit code segment */

/* Common segment access bytes */
#define GDT_KERNEL_CODE_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                 GDT_ACCESS_SEGMENT | GDT_ACCESS_EXEC | \
                                 GDT_ACCESS_RW)

#define GDT_KERNEL_DATA_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                 GDT_ACCESS_SEGMENT | GDT_ACCESS_RW)

#define GDT_USER_CODE_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                 GDT_ACCESS_SEGMENT | GDT_ACCESS_EXEC | \
                                 GDT_ACCESS_RW)

#define GDT_USER_DATA_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                 GDT_ACCESS_SEGMENT | GDT_ACCESS_RW)

#define GDT_TSS_ACCESS          (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                 GDT_TYPE_TSS_AVAIL)

/*
 * Per-CPU GDT and TSS
 *
 * Each CPU needs its own GDT (for the TSS entry) and TSS.
 */
struct cpu_gdt {
    struct gdt_entry entries[GDT_ENTRIES - 1];  /* Regular entries */
    struct tss_entry tss_entry;                  /* TSS (16 bytes) */
    struct gdt_ptr   gdtr;                       /* GDT register value */
    struct tss       tss;                        /* Task State Segment */
} __aligned(16);

/* Maximum CPUs supported */
#define MAX_CPUS            256

/* GDT functions */
void gdt_init(void);
void gdt_init_cpu(int cpu_id);
void gdt_load(struct gdt_ptr *gdtr);
void tss_load(u16 selector);
void tss_set_rsp0(u64 rsp0);
void tss_set_ist(int ist, u64 stack);

/* Get current CPU's TSS */
struct tss *tss_get_current(void);

#endif /* _OCEAN_GDT_H */
