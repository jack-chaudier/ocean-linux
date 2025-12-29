/*
 * Ocean Kernel - Global Descriptor Table Implementation
 *
 * Sets up the GDT with kernel/user segments and per-CPU TSS.
 */

#include <ocean/types.h>
#include <ocean/defs.h>
#include "gdt.h"

/* External functions */
extern void *memset(void *s, int c, size_t n);
extern int kprintf(const char *fmt, ...);

/* Per-CPU GDT structures */
static struct cpu_gdt cpu_gdts[MAX_CPUS] __aligned(16);

/* Current CPU ID (will be set properly with SMP init) */
static __attribute__((section(".data"))) int current_cpu_id = 0;

/*
 * Set a GDT entry
 */
static void gdt_set_entry(struct gdt_entry *entry, u32 base, u32 limit,
                          u8 access, u8 granularity)
{
    entry->base_low = base & 0xFFFF;
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high = (base >> 24) & 0xFF;

    entry->limit_low = limit & 0xFFFF;
    entry->granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);

    entry->access = access;
}

/*
 * Set the TSS entry (16-byte system descriptor)
 */
static void gdt_set_tss(struct tss_entry *entry, u64 base, u32 limit)
{
    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_middle = (base >> 16) & 0xFF;
    entry->access = GDT_TSS_ACCESS;
    entry->granularity = ((limit >> 16) & 0x0F);
    entry->base_high = (base >> 24) & 0xFF;
    entry->base_upper = (base >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}

/*
 * Initialize TSS for a CPU
 */
static void tss_init(struct tss *tss)
{
    memset(tss, 0, sizeof(struct tss));

    /* Set IOPB offset to beyond TSS size (no I/O bitmap) */
    tss->iopb_offset = sizeof(struct tss);

    /* RSP0 will be set when we switch to user mode */
    /* IST entries will be set up for specific interrupt handlers */
}

/*
 * Initialize GDT for a specific CPU
 */
void gdt_init_cpu(int cpu_id)
{
    struct cpu_gdt *gdt = &cpu_gdts[cpu_id];
    struct gdt_entry *entries = gdt->entries;

    /* Clear everything */
    memset(gdt, 0, sizeof(struct cpu_gdt));

    /*
     * Entry 0: Null descriptor (required by x86)
     */
    gdt_set_entry(&entries[0], 0, 0, 0, 0);

    /*
     * Entry 1 (0x08): Kernel Code Segment - 64-bit
     *
     * In long mode, base and limit are ignored.
     * Only the L bit (64-bit) and access flags matter.
     */
    gdt_set_entry(&entries[1],
                  0,                        /* Base (ignored) */
                  0xFFFFF,                  /* Limit (ignored) */
                  GDT_KERNEL_CODE_ACCESS,   /* Present, Ring 0, Code, RW */
                  GDT_GRAN_4K | GDT_GRAN_64BIT);  /* 4K gran, 64-bit */

    /*
     * Entry 2 (0x10): Kernel Data Segment
     *
     * Base and limit ignored in long mode.
     */
    gdt_set_entry(&entries[2],
                  0,
                  0xFFFFF,
                  GDT_KERNEL_DATA_ACCESS,   /* Present, Ring 0, Data, RW */
                  GDT_GRAN_4K | GDT_GRAN_32BIT);  /* Must be 32-bit for data */

    /*
     * Entry 3 (0x18): User Code Segment - 32-bit (compatibility mode)
     *
     * Used for 32-bit user processes (if supported).
     */
    gdt_set_entry(&entries[3],
                  0,
                  0xFFFFF,
                  GDT_USER_CODE_ACCESS,     /* Present, Ring 3, Code, RW */
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /*
     * Entry 4 (0x20): User Data Segment
     */
    gdt_set_entry(&entries[4],
                  0,
                  0xFFFFF,
                  GDT_USER_DATA_ACCESS,     /* Present, Ring 3, Data, RW */
                  GDT_GRAN_4K | GDT_GRAN_32BIT);

    /*
     * Entry 5 (0x28): User Code Segment - 64-bit
     */
    gdt_set_entry(&entries[5],
                  0,
                  0xFFFFF,
                  GDT_USER_CODE_ACCESS,     /* Present, Ring 3, Code, RW */
                  GDT_GRAN_4K | GDT_GRAN_64BIT);  /* 64-bit */

    /*
     * Entry 6 (0x30): TSS - 16-byte system descriptor
     */
    tss_init(&gdt->tss);
    gdt_set_tss(&gdt->tss_entry, (u64)&gdt->tss, sizeof(struct tss) - 1);

    /*
     * Set up GDTR
     */
    gdt->gdtr.limit = sizeof(gdt->entries) + sizeof(gdt->tss_entry) - 1;
    gdt->gdtr.base = (u64)&gdt->entries;

    kprintf("  CPU %d: GDT at 0x%llx, TSS at 0x%llx\n",
            cpu_id, (u64)&gdt->entries, (u64)&gdt->tss);
}

/*
 * Load GDT register
 */
void gdt_load(struct gdt_ptr *gdtr)
{
    __asm__ __volatile__(
        "lgdt (%0)\n\t"
        /* Reload code segment */
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        /* Reload data segments */
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "r"(gdtr), "i"((u64)KERNEL_CS), "i"((u16)KERNEL_DS)
        : "rax", "memory"
    );
}

/*
 * Load TSS register
 */
void tss_load(u16 selector)
{
    __asm__ __volatile__("ltr %0" : : "r"(selector));
}

/*
 * Set RSP0 in current CPU's TSS
 * This is the stack used when transitioning from user to kernel mode
 */
void tss_set_rsp0(u64 rsp0)
{
    cpu_gdts[current_cpu_id].tss.rsp0 = rsp0;
}

/*
 * Set an IST entry in current CPU's TSS
 * IST (Interrupt Stack Table) provides separate stacks for specific interrupts
 */
void tss_set_ist(int ist, u64 stack)
{
    if (ist < 1 || ist > 7) {
        return;
    }

    struct tss *tss = &cpu_gdts[current_cpu_id].tss;
    switch (ist) {
    case 1: tss->ist1 = stack; break;
    case 2: tss->ist2 = stack; break;
    case 3: tss->ist3 = stack; break;
    case 4: tss->ist4 = stack; break;
    case 5: tss->ist5 = stack; break;
    case 6: tss->ist6 = stack; break;
    case 7: tss->ist7 = stack; break;
    }
}

/*
 * Get current CPU's TSS
 */
struct tss *tss_get_current(void)
{
    return &cpu_gdts[current_cpu_id].tss;
}

/*
 * Initialize GDT for BSP (Bootstrap Processor)
 */
void gdt_init(void)
{
    kprintf("Initializing GDT...\n");

    /* Initialize GDT for CPU 0 (BSP) */
    gdt_init_cpu(0);

    /* Load GDT and TSS */
    gdt_load(&cpu_gdts[0].gdtr);
    tss_load(TSS_SELECTOR);

    kprintf("GDT loaded successfully\n");
}

/*
 * Set current CPU ID (called during SMP init)
 */
void gdt_set_cpu_id(int cpu_id)
{
    current_cpu_id = cpu_id;
}
