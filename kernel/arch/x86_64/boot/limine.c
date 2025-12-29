/*
 * Ocean Kernel - Limine Bootloader Interface
 *
 * Handles Limine boot protocol requests and kernel initialization.
 */

#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/boot.h>
#include <ocean/process.h>
#include "limine.h"

/* External functions */
extern void serial_early_init(void);
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/* Architecture initialization */
extern void gdt_init(void);
extern void idt_init(void);
extern void pic_remap(void);

/* Memory management */
extern void pmm_init(void);
extern void pmm_dump_stats(void);
extern void pmm_dump_free_areas(void);
extern void vmm_init(void);
extern void kheap_dump_stats(void);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Process and scheduler */
extern void process_init(void);
extern void sched_init(void);
extern void timer_init(void);
extern void schedule(void);
extern struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name);
extern void thread_start(struct thread *t);
extern void sched_dump_stats(void);
extern void process_dump(void);

/* Syscalls and user mode */
extern void syscall_init(void);
extern void exec_test_user_mode(void);
extern pid_t exec_elf(const void *elf_data, size_t elf_size, const char *name);

/* IPC */
extern void ipc_init(void);
extern void ipc_dump_stats(void);
extern void ipc_test(void);

/* External symbols from linker script */
extern char _bss_start[];
extern char _bss_end[];
extern char _kernel_start[];
extern char _kernel_end[];

/* Forward declarations */
static void kernel_main(void);
void panic(const char *fmt, ...) __noreturn;

/*
 * Limine requests - these must be in a specific section and format
 *
 * The requests are placed in .requests section and the linker
 * script ensures they are preserved.
 *
 * IMPORTANT: Must use the official macros which include LIMINE_COMMON_MAGIC
 */

/* Request markers - tell Limine where requests are */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER

/* Base revision - required for protocol version negotiation */
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2)

/* Bootloader info request */
__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0,
    .response = NULL
};

/* Higher-half direct map request */
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};

/* Memory map request */
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

/* Framebuffer request */
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

/* RSDP request (for ACPI) */
__attribute__((used, section(".requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0,
    .response = NULL
};

/* Kernel address request */
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL
};

/* SMP request */
__attribute__((used, section(".requests")))
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .response = NULL,
    .flags = 0  /* Request x2APIC if available */
};

/* Module request (for initrd) */
__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .response = NULL,
    .internal_module_count = 0,
    .internal_modules = NULL
};

/* Boot time request */
__attribute__((used, section(".requests")))
static volatile struct limine_boot_time_request boot_time_request = {
    .id = LIMINE_BOOT_TIME_REQUEST,
    .revision = 0,
    .response = NULL
};

/* Boot info structure instance (struct defined in ocean/boot.h) */
static struct boot_info boot_info;

/*
 * Get boot info structure
 */
const struct boot_info *get_boot_info(void)
{
    return &boot_info;
}

/*
 * Print memory map
 */
static void print_memory_map(void)
{
    static const char *memtype_names[] = {
        [LIMINE_MEMMAP_USABLE] = "Usable",
        [LIMINE_MEMMAP_RESERVED] = "Reserved",
        [LIMINE_MEMMAP_ACPI_RECLAIMABLE] = "ACPI Reclaimable",
        [LIMINE_MEMMAP_ACPI_NVS] = "ACPI NVS",
        [LIMINE_MEMMAP_BAD_MEMORY] = "Bad Memory",
        [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = "Bootloader Reclaimable",
        [LIMINE_MEMMAP_KERNEL_AND_MODULES] = "Kernel/Modules",
        [LIMINE_MEMMAP_FRAMEBUFFER] = "Framebuffer",
    };

    kprintf("\nMemory Map:\n");
    kprintf("  %-18s  %-18s  %s\n", "Base", "Length", "Type");
    kprintf("  --------------------------------------------------\n");

    u64 total_usable = 0;
    u64 total_memory = 0;

    for (u64 i = 0; i < boot_info.memmap_entries; i++) {
        struct limine_memmap_entry *entry =
            (struct limine_memmap_entry *)boot_info.memmap[i];
        const char *type_name = "Unknown";

        if (entry->type < ARRAY_SIZE(memtype_names) && memtype_names[entry->type]) {
            type_name = memtype_names[entry->type];
        }

        kprintf("  0x%016llx  0x%016llx  %s\n",
                entry->base, entry->length, type_name);

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_usable += entry->length;
        }
        total_memory += entry->length;
    }

    kprintf("\n  Total usable: %llu MiB / %llu MiB total\n",
            total_usable / (1024 * 1024), total_memory / (1024 * 1024));
}

/*
 * Kernel entry point - called by Limine
 */
void _start(void)
{
    /* Clear BSS */
    memset(_bss_start, 0, _bss_end - _bss_start);

    /* Initialize early serial console */
    serial_early_init();

    kprintf("\n");
    kprintf("==================================================\n");
    kprintf("  Ocean Microkernel v0.1.0\n");
    kprintf("  An educational x86_64 microkernel\n");
    kprintf("==================================================\n\n");

    /* Check base revision - Limine sets [2] to 0 if supported */
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        panic("Limine base revision not supported!");
    }

    /* Print bootloader info */
    if (bootloader_info_request.response) {
        kprintf("Bootloader: %s %s\n",
                bootloader_info_request.response->name,
                bootloader_info_request.response->version);
    }

    /* Get HHDM offset */
    if (!hhdm_request.response) {
        panic("No HHDM response from bootloader!");
    }
    boot_info.hhdm_offset = hhdm_request.response->offset;
    kprintf("HHDM offset: 0x%llx\n", boot_info.hhdm_offset);

    /* Get kernel address */
    if (!kernel_address_request.response) {
        panic("No kernel address response from bootloader!");
    }
    boot_info.kernel_phys_base = kernel_address_request.response->physical_base;
    boot_info.kernel_virt_base = kernel_address_request.response->virtual_base;
    kprintf("Kernel physical base: 0x%llx\n", boot_info.kernel_phys_base);
    kprintf("Kernel virtual base:  0x%llx\n", boot_info.kernel_virt_base);
    kprintf("Kernel size:          %llu KiB\n",
            ((u64)_kernel_end - (u64)_kernel_start) / 1024);

    /* Get memory map */
    if (!memmap_request.response) {
        panic("No memory map response from bootloader!");
    }
    boot_info.memmap = (void **)memmap_request.response->entries;
    boot_info.memmap_entries = memmap_request.response->entry_count;

    print_memory_map();

    /* Get RSDP */
    if (rsdp_request.response) {
        boot_info.rsdp = rsdp_request.response->address;
        kprintf("\nACPI RSDP at: 0x%p\n", boot_info.rsdp);
    } else {
        kprintf("\nNo ACPI RSDP found\n");
    }

    /* Get framebuffer */
    if (framebuffer_request.response &&
        framebuffer_request.response->framebuffer_count > 0) {
        boot_info.framebuffer = framebuffer_request.response->framebuffers[0];
        kprintf("\nFramebuffer: %llux%llu @ 0x%p, %d bpp\n",
                boot_info.framebuffer->width,
                boot_info.framebuffer->height,
                boot_info.framebuffer->address,
                boot_info.framebuffer->bpp);
    }

    /* Get SMP info */
    if (smp_request.response) {
        boot_info.cpu_count = smp_request.response->cpu_count;
        boot_info.bsp_lapic_id = smp_request.response->bsp_lapic_id;
        boot_info.cpus = smp_request.response->cpus;
        kprintf("\nCPUs: %llu (BSP LAPIC ID: %u)\n",
                boot_info.cpu_count, boot_info.bsp_lapic_id);
    } else {
        boot_info.cpu_count = 1;
        kprintf("\nSingle CPU (no SMP response)\n");
    }

    /* Get modules and cache them (before memory reclaim) */
    if (module_request.response) {
        boot_info.modules = module_request.response->modules;
        boot_info.module_count = module_request.response->module_count;
        kprintf("\nModules loaded: %llu\n", boot_info.module_count);

        /* Cache module info to survive bootloader memory reclaim */
        boot_info.cached_module_count = 0;
        for (u64 i = 0; i < boot_info.module_count && i < MAX_MODULES; i++) {
            struct limine_file *mod = boot_info.modules[i];
            boot_info.cached_modules[i].address = mod->address;
            boot_info.cached_modules[i].size = mod->size;

            /* Copy cmdline */
            const char *src = mod->cmdline ? mod->cmdline : mod->path;
            for (int j = 0; j < 63 && src && src[j]; j++) {
                boot_info.cached_modules[i].cmdline[j] = src[j];
                boot_info.cached_modules[i].cmdline[j+1] = '\0';
            }

            kprintf("  [%llu] %s (%llu bytes)\n", i,
                    boot_info.cached_modules[i].cmdline,
                    boot_info.cached_modules[i].size);
            boot_info.cached_module_count++;
        }
    }

    /* Get boot time */
    if (boot_time_request.response) {
        boot_info.boot_time = boot_time_request.response->boot_time;
        kprintf("\nBoot time: %lld (Unix timestamp)\n", boot_info.boot_time);
    }

    kprintf("\n");

    /* Continue with kernel initialization */
    kernel_main();

    /* Should never reach here */
    panic("kernel_main() returned!");
}

/*
 * Kernel main function - called after initial Limine setup
 */
static void kernel_main(void)
{
    kprintf("Entering kernel_main()...\n\n");

    /*
     * Phase 1: CPU Setup
     */
    kprintf("=== Phase 1: CPU Setup ===\n");

    /* Initialize GDT with TSS */
    gdt_init();

    /* Remap PIC to avoid vector conflicts */
    pic_remap();

    /* Initialize IDT with exception and IRQ handlers */
    idt_init();

    /* Enable interrupts */
    kprintf("Enabling interrupts...\n");
    sti();

    kprintf("\nPhase 1 complete: CPU initialized\n\n");

    /*
     * Phase 2: Memory Setup
     */
    kprintf("=== Phase 2: Memory Setup ===\n");

    /* Initialize Physical Memory Manager */
    pmm_init();

    /* Dump PMM stats for verification */
    pmm_dump_stats();

    /* Initialize Virtual Memory Manager (includes kernel heap) */
    vmm_init();

    /* Test kernel heap */
    kprintf("\nTesting kernel heap (kmalloc/kfree)...\n");
    void *test1 = kmalloc(64);
    void *test2 = kmalloc(128);
    void *test3 = kmalloc(256);
    kprintf("  Allocated: %p, %p, %p\n", test1, test2, test3);
    kfree(test1);
    kfree(test2);
    kfree(test3);
    kprintf("  Freed successfully\n");

    kheap_dump_stats();

    kprintf("\nPhase 2 complete: Memory initialized\n");

    /*
     * Phase 3: Core Services
     */
    kprintf("\n=== Phase 3: Core Services ===\n");

    /* Initialize process subsystem */
    process_init();

    /* Initialize scheduler */
    sched_init();

    /* Initialize timer (provides preemption) */
    timer_init();

    kprintf("\nPhase 3 complete: Scheduler initialized\n");

    /*
     * Phase 4: User Space & IPC
     */
    kprintf("\n=== Phase 4: User Space & IPC ===\n");

    /* Initialize system calls */
    syscall_init();

    /* Initialize IPC subsystem */
    ipc_init();

    kprintf("\n");
    kprintf("==================================================\n");
    kprintf("  Kernel initialization complete!\n");
    kprintf("==================================================\n\n");

    /* Test IPC between kernel threads */
    ipc_test();

    /* Dump scheduler stats */
    sched_dump_stats();

    /*
     * Phase 5: Start Init Process
     */
    kprintf("\n=== Phase 5: Starting Init ===\n");

    /* Look for init module loaded by bootloader (use cached copy) */
    pid_t init_pid = -1;

    if (boot_info.cached_module_count > 0) {
        for (u64 i = 0; i < boot_info.cached_module_count; i++) {
            struct cached_module *mod = &boot_info.cached_modules[i];
            kprintf("Checking module: %s\n", mod->cmdline);

            /* Look for init module */
            const char *cmdline = mod->cmdline;
            /* Check if cmdline contains "init" */
            if ((cmdline[0] == 'i' && cmdline[1] == 'n' &&
                 cmdline[2] == 'i' && cmdline[3] == 't') ||
                (cmdline[0] == '/' && cmdline[1] == 'i')) {

                kprintf("Found init module at %p, size %llu bytes\n",
                        mod->address, mod->size);

                /* Execute init */
                init_pid = exec_elf(mod->address, mod->size, "init");

                if (init_pid > 0) {
                    kprintf("Init started with PID %d\n", init_pid);
                } else {
                    kprintf("Failed to start init!\n");
                }
                break;
            }
        }
    }

    if (init_pid <= 0) {
        kprintf("No init module found, running test program...\n");
        exec_test_user_mode();
    }

    kprintf("\nEntering idle loop...\n");

    /* Enter idle loop - scheduler will take over */
    for (;;) {
        /* Check if there's a pending reschedule */
        extern struct thread *current_thread;
        if (current_thread && (current_thread->flags & TF_NEED_RESCHED)) {
            current_thread->flags &= ~TF_NEED_RESCHED;
            schedule();
        }

        /* Enable interrupts and halt until next interrupt */
        __asm__ __volatile__(
            "sti\n\t"
            "hlt\n\t"
            "cli\n\t"
            ::: "memory"
        );
    }
}

/*
 * Kernel panic - print message and halt
 */
void panic(const char *fmt, ...)
{
    va_list ap;

    cli();

    kprintf("\n");
    kprintf("!!! KERNEL PANIC !!!\n");
    kprintf("-------------------\n");

    va_start(ap, fmt);
    /* Simple panic print - direct to serial */
    extern int kvprintf(const char *fmt, va_list ap);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\n\nSystem halted.\n");

    halt_forever();
}
