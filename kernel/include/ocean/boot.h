/*
 * Ocean Kernel - Boot Information
 *
 * Shared boot information structures populated by the bootloader interface.
 */

#ifndef _OCEAN_BOOT_H
#define _OCEAN_BOOT_H

#include <ocean/types.h>

/* Forward declarations for Limine structures */
struct limine_memmap_entry;
struct limine_framebuffer;
struct limine_smp_info;
struct limine_file;

/*
 * Cached module info
 * Copied early before bootloader memory is reclaimed
 */
#define MAX_MODULES 8

struct cached_module {
    void *address;      /* Module data address */
    u64 size;           /* Module size */
    char cmdline[64];   /* Module command line */
};

/*
 * Limine memory map entry types
 */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

/*
 * Memory map entry (compatible with Limine format)
 */
struct memmap_entry {
    u64 base;
    u64 length;
    u64 type;
};

/*
 * Boot information structure
 *
 * Populated by the Limine bootloader interface and made available
 * to the rest of the kernel.
 */
struct boot_info {
    /* Memory map (pointers to entries, cast to struct memmap_entry*) */
    void **memmap;
    u64 memmap_entries;

    /* Higher-half direct map offset */
    u64 hhdm_offset;

    /* Kernel address info */
    u64 kernel_phys_base;
    u64 kernel_virt_base;

    /* Framebuffer */
    struct limine_framebuffer *framebuffer;

    /* ACPI RSDP */
    void *rsdp;

    /* SMP info */
    u64 cpu_count;
    u32 bsp_lapic_id;
    struct limine_smp_info **cpus;

    /* Boot time */
    i64 boot_time;

    /* Modules (initrd) - original pointers (may become invalid) */
    struct limine_file **modules;
    u64 module_count;

    /* Cached modules (safe to use after memory init) */
    struct cached_module cached_modules[MAX_MODULES];
    u64 cached_module_count;
};

/* Get the boot info structure (implemented in limine.c) */
const struct boot_info *get_boot_info(void);

#endif /* _OCEAN_BOOT_H */
