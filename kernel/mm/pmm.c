/*
 * Ocean Kernel - Physical Memory Manager
 *
 * Main PMM initialization and page allocation interface.
 * Integrates the buddy allocator, memory bitmap, and per-CPU caches.
 */

#include <ocean/pmm.h>
#include <ocean/boot.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/* Buddy allocator functions */
extern void buddy_init_zone(struct zone *zone);
extern void buddy_add_pages(struct zone *zone, pfn_t start_pfn, u64 nr_pages);
extern struct page *buddy_alloc_pages(struct zone *zone, unsigned int order);
extern void buddy_free_pages(struct zone *zone, struct page *page, unsigned int order);
extern void buddy_dump_free_areas(struct zone *zone);
extern void buddy_verify_integrity(struct zone *zone);

/* Memory bitmap functions */
extern void mem_bitmap_init(pfn_t max_pfn, void *bitmap_memory);
extern void mem_bitmap_mark_usable(phys_addr_t start, phys_addr_t end);
extern void mem_bitmap_mark_reserved(phys_addr_t start, phys_addr_t end);
extern bool mem_bitmap_is_usable(pfn_t pfn);
extern u64 mem_bitmap_size_for(pfn_t max_pfn);
extern void mem_bitmap_dump_stats(void);
extern void mem_bitmap_dump_visual(void);

/* Global PMM state */
struct pmm_state pmm;

/* Zone names */
static const char *zone_names[MAX_ZONES] = {
    [ZONE_DMA]    = "DMA",
    [ZONE_DMA32]  = "DMA32",
    [ZONE_NORMAL] = "Normal"
};

/* Zone boundaries */
#define ZONE_DMA_END    (16ULL * 1024 * 1024)       /* 16 MiB */
#define ZONE_DMA32_END  (4ULL * 1024 * 1024 * 1024) /* 4 GiB */

/*
 * Early boot allocator
 *
 * Used before the buddy allocator is ready. Simply bumps a pointer.
 */
static struct {
    phys_addr_t base;
    phys_addr_t current;
    phys_addr_t end;
} boot_alloc;

static void *boot_alloc_pages(u64 nr_pages)
{
    phys_addr_t addr = PAGE_ALIGN(boot_alloc.current);
    phys_addr_t size = nr_pages * PAGE_SIZE;

    if (addr + size > boot_alloc.end) {
        kprintf("PANIC: Boot allocator out of memory!\n");
        return NULL;
    }

    boot_alloc.current = addr + size;

    /* Return virtual address via HHDM */
    return phys_to_virt(addr);
}

/*
 * Determine which zone a physical address belongs to
 */
static enum zone_type pfn_to_zone_type(pfn_t pfn)
{
    phys_addr_t addr = PFN_TO_PHYS(pfn);

    if (addr < ZONE_DMA_END) {
        return ZONE_DMA;
    } else if (addr < ZONE_DMA32_END) {
        return ZONE_DMA32;
    } else {
        return ZONE_NORMAL;
    }
}

/*
 * Initialize zones
 */
static void init_zones(void)
{
    for (int i = 0; i < MAX_ZONES; i++) {
        struct zone *zone = &pmm.zones[i];

        zone->name = zone_names[i];
        zone->start_pfn = 0;
        zone->end_pfn = 0;
        zone->present_pages = 0;
        zone->free_pages = 0;
        zone->alloc_count = 0;
        zone->free_count = 0;
        zone->pcpu_caches = NULL;

        buddy_init_zone(zone);
    }

    /* Set zone boundaries */
    pmm.zones[ZONE_DMA].start_pfn = 0;
    pmm.zones[ZONE_DMA].end_pfn = PHYS_TO_PFN(ZONE_DMA_END);

    pmm.zones[ZONE_DMA32].start_pfn = PHYS_TO_PFN(ZONE_DMA_END);
    pmm.zones[ZONE_DMA32].end_pfn = PHYS_TO_PFN(ZONE_DMA32_END);

    pmm.zones[ZONE_NORMAL].start_pfn = PHYS_TO_PFN(ZONE_DMA32_END);
    pmm.zones[ZONE_NORMAL].end_pfn = pmm.page_array_pfns;
}

/*
 * Initialize the page array
 *
 * Allocates one struct page for every physical page frame.
 */
static void init_page_array(pfn_t max_pfn)
{
    u64 array_size = max_pfn * sizeof(struct page);
    u64 array_pages = PAGE_ALIGN(array_size) / PAGE_SIZE;

    kprintf("  Page array: %llu pages, %llu MiB\n",
            max_pfn, array_size / (1024 * 1024));

    /* Allocate from boot allocator */
    pmm.page_array = boot_alloc_pages(array_pages);
    if (!pmm.page_array) {
        kprintf("PANIC: Failed to allocate page array!\n");
        return;
    }

    pmm.page_array_pfns = max_pfn;

    /* Initialize all pages as reserved */
    memset(pmm.page_array, 0, array_size);
    for (pfn_t pfn = 0; pfn < max_pfn; pfn++) {
        struct page *page = &pmm.page_array[pfn];
        page->flags = PG_RESERVED;
        page->zone_id = pfn_to_zone_type(pfn);
        INIT_LIST_HEAD(&page->buddy_list);
    }
}

/*
 * Add usable memory regions to the buddy allocator
 */
static void add_usable_memory(void)
{
    /* We need access to the Limine memory map */
    /* For now, we'll use a simplified approach based on boot_info */

    kprintf("  Adding usable memory to buddy allocator...\n");

    /*
     * Walk through the page array and add usable pages to zones
     * A page is usable if it's not reserved (marked by bitmap)
     */
    pfn_t run_start = 0;
    u64 run_length = 0;
    enum zone_type current_zone = ZONE_DMA;

    for (pfn_t pfn = 0; pfn < pmm.page_array_pfns; pfn++) {
        struct page *page = &pmm.page_array[pfn];
        enum zone_type zone = pfn_to_zone_type(pfn);

        bool is_usable = !(page->flags & PG_RESERVED);

        if (is_usable && zone == current_zone) {
            /* Continue the current run */
            if (run_length == 0) {
                run_start = pfn;
            }
            run_length++;
        } else {
            /* End the current run */
            if (run_length > 0) {
                buddy_add_pages(&pmm.zones[current_zone], run_start, run_length);
                pmm.zones[current_zone].present_pages += run_length;
            }

            /* Start new run if this page is usable */
            if (is_usable) {
                run_start = pfn;
                run_length = 1;
                current_zone = zone;
            } else {
                run_length = 0;
            }
        }
    }

    /* Add final run */
    if (run_length > 0) {
        buddy_add_pages(&pmm.zones[current_zone], run_start, run_length);
        pmm.zones[current_zone].present_pages += run_length;
    }
}

/*
 * Initialize the Physical Memory Manager
 *
 * This is called early in kernel initialization with the memory map
 * from the bootloader (Limine).
 */
void pmm_init(void)
{
    kprintf("Initializing Physical Memory Manager...\n");

    /* Get boot info */
    const struct boot_info *boot = get_boot_info();
    if (!boot) {
        kprintf("PANIC: No boot info available!\n");
        return;
    }

    /* Store HHDM offset */
    pmm.hhdm_offset = boot->hhdm_offset;
    kprintf("  HHDM offset: 0x%llx\n", pmm.hhdm_offset);

    /*
     * Find memory bounds and the largest usable region for boot allocator
     *
     * Important: Only consider usable/reclaimable regions for max_addr!
     * Reserved regions (like PCI MMIO at high addresses) should not
     * expand our page array unnecessarily.
     */
    phys_addr_t max_addr = 0;
    phys_addr_t boot_alloc_base = 0;
    phys_addr_t boot_alloc_size = 0;

    kprintf("  Scanning memory map (%llu entries)...\n", boot->memmap_entries);

    for (u64 i = 0; i < boot->memmap_entries; i++) {
        struct memmap_entry *entry = (struct memmap_entry *)boot->memmap[i];

        /* Only count usable memory types when determining max address */
        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) {
            phys_addr_t entry_end = entry->base + entry->length;
            if (entry_end > max_addr) {
                max_addr = entry_end;
            }
        }

        /* Find largest usable region for boot allocator */
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length > boot_alloc_size) {
                boot_alloc_base = entry->base;
                boot_alloc_size = entry->length;
            }
        }
    }

    pfn_t max_pfn = PHYS_TO_PFN(max_addr);
    kprintf("  Max physical address: 0x%llx (%llu MiB)\n",
            max_addr, max_addr / (1024 * 1024));
    kprintf("  Max PFN: %llu\n", max_pfn);

    /*
     * Initialize boot allocator
     */
    boot_alloc.base = boot_alloc_base;
    boot_alloc.current = boot_alloc_base;
    boot_alloc.end = boot_alloc_base + boot_alloc_size;
    kprintf("  Boot allocator: 0x%llx - 0x%llx (%llu MiB)\n",
            boot_alloc_base, boot_alloc.end,
            boot_alloc_size / (1024 * 1024));

    /*
     * Allocate and initialize the memory bitmap
     */
    u64 bitmap_size = mem_bitmap_size_for(max_pfn);
    void *bitmap_mem = boot_alloc_pages((bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!bitmap_mem) {
        kprintf("PANIC: Failed to allocate memory bitmap!\n");
        return;
    }
    mem_bitmap_init(max_pfn, bitmap_mem);

    /*
     * Mark memory regions in bitmap based on Limine memory map
     */
    for (u64 i = 0; i < boot->memmap_entries; i++) {
        struct memmap_entry *entry = (struct memmap_entry *)boot->memmap[i];

        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            mem_bitmap_mark_usable(entry->base, entry->base + entry->length);
        }
    }

    /*
     * Reserve boot allocator region (what we've used so far)
     */
    mem_bitmap_mark_reserved(boot_alloc.base, boot_alloc.current);

    /*
     * Initialize the page array
     */
    init_page_array(max_pfn);

    /*
     * Reserve the page array region
     */
    phys_addr_t page_array_phys = virt_to_phys(pmm.page_array);
    u64 page_array_size = max_pfn * sizeof(struct page);
    mem_bitmap_mark_reserved(page_array_phys, page_array_phys + page_array_size);

    /*
     * Mark usable pages in the page array
     */
    for (pfn_t pfn = 0; pfn < max_pfn; pfn++) {
        if (mem_bitmap_is_usable(pfn)) {
            pmm.page_array[pfn].flags &= ~PG_RESERVED;
            pmm.free_pages++;
        } else {
            pmm.reserved_pages++;
        }
        pmm.total_pages++;
    }

    /*
     * Initialize memory zones
     */
    init_zones();

    /*
     * Add usable memory to buddy allocator
     */
    add_usable_memory();

    /*
     * Calculate total free pages from zones
     */
    pmm.free_pages = 0;
    for (int i = 0; i < MAX_ZONES; i++) {
        pmm.free_pages += pmm.zones[i].free_pages;
    }

    pmm.initialized = true;

    kprintf("PMM initialized:\n");
    kprintf("  Total pages:    %llu (%llu MiB)\n",
            pmm.total_pages, (pmm.total_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Free pages:     %llu (%llu MiB)\n",
            pmm.free_pages, (pmm.free_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Reserved pages: %llu (%llu MiB)\n",
            pmm.reserved_pages, (pmm.reserved_pages * PAGE_SIZE) / (1024 * 1024));

    for (int i = 0; i < MAX_ZONES; i++) {
        struct zone *zone = &pmm.zones[i];
        if (zone->present_pages > 0) {
            kprintf("  Zone %s: %llu present, %llu free\n",
                    zone->name, zone->present_pages, zone->free_pages);
        }
    }
}

/*
 * Allocate pages from the appropriate zone
 */
struct page *alloc_pages(unsigned int order, unsigned int gfp_flags)
{
    struct page *page = NULL;
    enum zone_type start_zone = ZONE_NORMAL;

    if (!pmm.initialized) {
        return NULL;
    }

    /* Determine starting zone based on flags */
    if (gfp_flags & GFP_DMA) {
        start_zone = ZONE_DMA;
    } else if (gfp_flags & GFP_DMA32) {
        start_zone = ZONE_DMA32;
    }

    /* Try zones from highest to lowest (fall back to lower zones if needed) */
    for (int zone_id = start_zone; zone_id >= 0; zone_id--) {
        struct zone *zone = &pmm.zones[zone_id];

        if (zone->free_pages < (1UL << order)) {
            continue;
        }

        page = buddy_alloc_pages(zone, order);
        if (page) {
            break;
        }
    }

    if (!page) {
        return NULL;
    }

    /* Set up the page */
    page->flags &= ~PG_BUDDY;
    if (gfp_flags & GFP_KERNEL) {
        page->flags |= PG_KERNEL;
    }

    /* Zero if requested */
    if (gfp_flags & GFP_ZERO) {
        void *addr = phys_to_virt(page_to_phys(page));
        memset(addr, 0, (1UL << order) * PAGE_SIZE);
    }

    /* Mark compound page if order > 0 */
    if (order > 0) {
        page->flags |= PG_HEAD | PG_COMPOUND;
        page->order = order;
        for (unsigned int i = 1; i < (1U << order); i++) {
            struct page *tail = page + i;
            tail->flags |= PG_TAIL | PG_COMPOUND;
            tail->head = page;
        }
    }

    return page;
}

/*
 * Allocate a single page
 */
struct page *alloc_page(unsigned int gfp_flags)
{
    return alloc_pages(0, gfp_flags);
}

/*
 * Free pages back to the buddy allocator
 */
void free_pages(struct page *page, unsigned int order)
{
    if (!page || !pmm.initialized) {
        return;
    }

    enum zone_type zone_type = page->zone_id;
    struct zone *zone = &pmm.zones[zone_type];

    /* Clear compound page flags */
    if (order > 0) {
        page->flags &= ~(PG_HEAD | PG_COMPOUND);
        for (unsigned int i = 1; i < (1U << order); i++) {
            struct page *tail = page + i;
            tail->flags &= ~(PG_TAIL | PG_COMPOUND);
            tail->head = NULL;
        }
    }

    page->flags &= ~PG_KERNEL;

    buddy_free_pages(zone, page, order);
}

/*
 * Free a single page
 */
void free_page(struct page *page)
{
    free_pages(page, 0);
}

/*
 * Convenience wrapper: allocate and return virtual address
 */
void *get_free_pages(unsigned int order, unsigned int gfp_flags)
{
    struct page *page = alloc_pages(order, gfp_flags);
    if (!page) {
        return NULL;
    }
    return phys_to_virt(page_to_phys(page));
}

void *get_free_page(unsigned int gfp_flags)
{
    return get_free_pages(0, gfp_flags);
}

void *get_zeroed_page(unsigned int gfp_flags)
{
    return get_free_page(gfp_flags | GFP_ZERO);
}

/*
 * Allocate from a specific zone
 */
struct page *alloc_pages_zone(enum zone_type zone, unsigned int order,
                              unsigned int gfp_flags)
{
    if (zone >= MAX_ZONES) {
        return NULL;
    }

    struct page *page = buddy_alloc_pages(&pmm.zones[zone], order);
    if (!page) {
        return NULL;
    }

    page->flags &= ~PG_BUDDY;
    if (gfp_flags & GFP_ZERO) {
        void *addr = phys_to_virt(page_to_phys(page));
        memset(addr, 0, (1UL << order) * PAGE_SIZE);
    }

    return page;
}

/*
 * Reserve a physical memory range
 */
void pmm_reserve_range(phys_addr_t start, phys_addr_t end)
{
    pfn_t start_pfn = PHYS_TO_PFN(PAGE_ALIGN_DOWN(start));
    pfn_t end_pfn = PHYS_TO_PFN(PAGE_ALIGN(end));

    for (pfn_t pfn = start_pfn; pfn < end_pfn && pfn < pmm.page_array_pfns; pfn++) {
        pmm.page_array[pfn].flags |= PG_RESERVED;
    }

    mem_bitmap_mark_reserved(start, end);
}

/*
 * Statistics
 */
u64 pmm_get_free_pages(void)
{
    u64 total = 0;
    for (int i = 0; i < MAX_ZONES; i++) {
        total += pmm.zones[i].free_pages;
    }
    return total;
}

u64 pmm_get_total_pages(void)
{
    return pmm.total_pages;
}

void pmm_dump_stats(void)
{
    kprintf("\nPMM Statistics:\n");
    kprintf("  Total pages:    %llu (%llu MiB)\n",
            pmm.total_pages, (pmm.total_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Free pages:     %llu (%llu MiB)\n",
            pmm_get_free_pages(),
            (pmm_get_free_pages() * PAGE_SIZE) / (1024 * 1024));

    for (int i = 0; i < MAX_ZONES; i++) {
        struct zone *zone = &pmm.zones[i];
        if (zone->present_pages > 0) {
            kprintf("\n  Zone %s:\n", zone->name);
            kprintf("    Present: %llu pages\n", zone->present_pages);
            kprintf("    Free:    %llu pages\n", zone->free_pages);
            kprintf("    Allocs:  %llu\n", zone->alloc_count);
            kprintf("    Frees:   %llu\n", zone->free_count);
        }
    }
}

void pmm_dump_free_areas(void)
{
    kprintf("\nBuddy Allocator Free Areas:\n");
    for (int i = 0; i < MAX_ZONES; i++) {
        struct zone *zone = &pmm.zones[i];
        if (zone->present_pages > 0) {
            buddy_dump_free_areas(zone);
        }
    }
}

void pmm_verify_integrity(void)
{
    kprintf("Verifying PMM integrity...\n");
    for (int i = 0; i < MAX_ZONES; i++) {
        struct zone *zone = &pmm.zones[i];
        if (zone->present_pages > 0) {
            buddy_verify_integrity(zone);
        }
    }
    kprintf("PMM integrity check complete.\n");
}

/*
 * Per-CPU cache initialization (placeholder)
 */
void pmm_init_pcpu_cache(int cpu_id)
{
    (void)cpu_id;
    /* TODO: Implement per-CPU page caches */
}

void pmm_drain_pcpu_cache(int cpu_id)
{
    (void)cpu_id;
    /* TODO: Implement per-CPU page caches */
}

/*
 * Simplified allocation wrappers for VMM/slab use
 * These use default GFP flags and take/return virtual addresses
 */

/* Get virtual address to struct page conversion */
static inline struct page *virt_to_page(void *addr)
{
    phys_addr_t phys = virt_to_phys(addr);
    pfn_t pfn = PHYS_TO_PFN(phys);
    if (pfn >= pmm.page_array_pfns) {
        return NULL;
    }
    return &pmm.page_array[pfn];
}

/* Simple allocation without GFP flags */
void *simple_get_free_page(void)
{
    return get_free_page(GFP_KERNEL);
}

void *simple_get_free_pages(unsigned int order)
{
    return get_free_pages(order, GFP_KERNEL);
}

/* Free page by virtual address */
void simple_free_page(void *addr)
{
    if (!addr) return;
    struct page *page = virt_to_page(addr);
    if (page) {
        free_page(page);
    }
}

void simple_free_pages(void *addr, unsigned int order)
{
    if (!addr) return;
    struct page *page = virt_to_page(addr);
    if (page) {
        free_pages(page, order);
    }
}
