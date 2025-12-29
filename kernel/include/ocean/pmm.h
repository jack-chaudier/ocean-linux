/*
 * Ocean Kernel - Physical Memory Manager
 *
 * Hybrid buddy allocator + bitmap for physical page management.
 *
 * Design:
 *   - Buddy allocator provides O(1) amortized allocation with coalescing
 *   - Per-CPU page caches eliminate lock contention for single pages
 *   - Bitmap tracks memory holes, reserved regions, and page states
 *   - struct page array provides per-page metadata
 */

#ifndef _OCEAN_PMM_H
#define _OCEAN_PMM_H

#include <ocean/types.h>
#include <ocean/list.h>
#include <ocean/spinlock.h>

/*
 * Page size constants
 */
#define PAGE_SHIFT          12
#define PAGE_SIZE           (1UL << PAGE_SHIFT)     /* 4 KiB */
#define PAGE_MASK           (~(PAGE_SIZE - 1))

#define LARGE_PAGE_SHIFT    21
#define LARGE_PAGE_SIZE     (1UL << LARGE_PAGE_SHIFT)  /* 2 MiB */

#define HUGE_PAGE_SHIFT     30
#define HUGE_PAGE_SIZE      (1UL << HUGE_PAGE_SHIFT)   /* 1 GiB */

/*
 * Address conversion macros
 * Note: PAGE_ALIGN and PAGE_ALIGN_DOWN are defined in defs.h
 */
#define PFN_TO_PHYS(pfn)        ((phys_addr_t)(pfn) << PAGE_SHIFT)
#define PHYS_TO_PFN(phys)       ((pfn_t)((phys) >> PAGE_SHIFT))

/*
 * Buddy allocator constants
 *
 * MAX_ORDER = 11 means we can allocate up to 2^10 = 1024 contiguous pages
 * which is 4 MiB per allocation (sufficient for most kernel needs)
 */
#define MAX_ORDER           11
#define MAX_ORDER_PAGES     (1UL << (MAX_ORDER - 1))

/*
 * Per-CPU page cache
 *
 * Each CPU maintains a small cache of free pages to avoid
 * contention on the global buddy allocator for single-page allocations.
 */
#define PCPU_CACHE_SIZE     64      /* Pages per CPU cache */
#define PCPU_BATCH_SIZE     16      /* Pages to refill/drain at once */

/*
 * Memory zones
 *
 * Different zones for different purposes:
 *   - ZONE_DMA: 0-16MB, for legacy DMA devices
 *   - ZONE_DMA32: 0-4GB, for 32-bit DMA devices
 *   - ZONE_NORMAL: All remaining memory
 */
enum zone_type {
    ZONE_DMA,           /* 0 - 16 MiB */
    ZONE_DMA32,         /* 16 MiB - 4 GiB */
    ZONE_NORMAL,        /* 4 GiB+ */
    MAX_ZONES
};

/*
 * Page flags
 *
 * Stored in struct page to track page state.
 */
#define PG_RESERVED     (1 << 0)    /* Page is reserved (kernel, MMIO, etc.) */
#define PG_BUDDY        (1 << 1)    /* Page is in buddy system free list */
#define PG_SLAB         (1 << 2)    /* Page is used by slab allocator */
#define PG_COMPOUND     (1 << 3)    /* Part of compound (multi-page) allocation */
#define PG_HEAD         (1 << 4)    /* First page of compound allocation */
#define PG_TAIL         (1 << 5)    /* Tail page of compound allocation */
#define PG_LOCKED       (1 << 6)    /* Page is locked (for I/O, etc.) */
#define PG_DIRTY        (1 << 7)    /* Page has been modified */
#define PG_REFERENCED   (1 << 8)    /* Page has been accessed recently */
#define PG_ACTIVE       (1 << 9)    /* Page is on active LRU list */
#define PG_KERNEL       (1 << 10)   /* Kernel allocation (not user) */

/*
 * Allocation flags (GFP = Get Free Pages)
 */
#define GFP_KERNEL      0x00        /* Normal kernel allocation */
#define GFP_ATOMIC      0x01        /* Cannot sleep, from interrupt context */
#define GFP_DMA         0x02        /* Need DMA-capable memory (< 16MB) */
#define GFP_DMA32       0x04        /* Need 32-bit addressable memory */
#define GFP_ZERO        0x08        /* Zero the allocated pages */
#define GFP_HIGH        0x10        /* High-priority allocation */
#define GFP_USER        0x20        /* User allocation (not kernel) */

/*
 * struct page - Per-page metadata
 *
 * One struct page exists for every physical page frame.
 * This is the core data structure for memory management.
 *
 * Size: 64 bytes (fits nicely in cache line)
 */
struct page {
    u32 flags;                  /* Page flags (PG_*) */
    u32 order;                  /* Buddy order (0-10) when free */

    union {
        /* When page is free in buddy system */
        struct {
            struct list_head buddy_list;    /* Link in free_area list */
        };

        /* When page is allocated */
        struct {
            u32 refcount;               /* Reference count */
            u32 mapcount;               /* Number of page table mappings */
            void *private;              /* Private data (e.g., slab info) */
        };
    };

    /* Zone this page belongs to */
    u8 zone_id;

    /* Padding to 64 bytes */
    u8 _pad[7];

    /* For compound pages: pointer to head page */
    struct page *head;

    /* Virtual address where this page is mapped (if any) */
    void *virtual;
} __aligned(64);

/*
 * Free area - one per order in buddy allocator
 */
struct free_area {
    struct list_head free_list;     /* List of free page blocks */
    u64 nr_free;                    /* Number of free blocks */
};

/*
 * Per-CPU page cache
 */
struct pcpu_cache {
    u32 count;                      /* Number of pages in cache */
    struct page *pages[PCPU_CACHE_SIZE];
} __aligned(64);

/*
 * Memory zone
 */
struct zone {
    const char *name;               /* Zone name for debugging */

    /* Zone boundaries */
    pfn_t start_pfn;                /* First PFN in zone */
    pfn_t end_pfn;                  /* Last PFN + 1 */
    u64 present_pages;              /* Total pages in zone */
    u64 free_pages;                 /* Currently free pages */

    /* Buddy allocator */
    struct free_area free_area[MAX_ORDER];
    spinlock_t lock;

    /* Per-CPU caches (indexed by CPU ID) */
    struct pcpu_cache *pcpu_caches;

    /* Statistics */
    u64 alloc_count;                /* Total allocations */
    u64 free_count;                 /* Total frees */
};

/*
 * Physical memory manager state
 */
struct pmm_state {
    /* Page array - one struct page per physical page */
    struct page *page_array;
    pfn_t page_array_pfns;          /* Number of entries in page_array */

    /* Physical memory bounds */
    phys_addr_t phys_start;         /* First usable physical address */
    phys_addr_t phys_end;           /* Last usable physical address + 1 */

    /* Memory zones */
    struct zone zones[MAX_ZONES];

    /* Total memory statistics */
    u64 total_pages;                /* Total physical pages */
    u64 free_pages;                 /* Currently free pages */
    u64 reserved_pages;             /* Reserved pages (kernel, MMIO) */

    /* Higher-half direct map offset (from bootloader) */
    u64 hhdm_offset;

    /* Initialization complete flag */
    bool initialized;
};

/* Global PMM state */
extern struct pmm_state pmm;

/*
 * Physical/virtual address conversion using HHDM
 *
 * The bootloader sets up a higher-half direct map (HHDM) that maps
 * all physical memory at a fixed virtual offset. This allows easy
 * conversion between physical and virtual addresses.
 */
static inline void *phys_to_virt(phys_addr_t phys)
{
    return (void *)(phys + pmm.hhdm_offset);
}

static inline phys_addr_t virt_to_phys(void *virt)
{
    return (phys_addr_t)virt - pmm.hhdm_offset;
}

/*
 * Page/PFN conversion
 */
static inline struct page *pfn_to_page(pfn_t pfn)
{
    return &pmm.page_array[pfn];
}

static inline pfn_t page_to_pfn(struct page *page)
{
    return (pfn_t)(page - pmm.page_array);
}

static inline phys_addr_t page_to_phys(struct page *page)
{
    return PFN_TO_PHYS(page_to_pfn(page));
}

static inline struct page *phys_to_page(phys_addr_t phys)
{
    return pfn_to_page(PHYS_TO_PFN(phys));
}

/*
 * Page flag helpers
 */
static inline bool page_is_free(struct page *page)
{
    return (page->flags & PG_BUDDY) != 0;
}

static inline bool page_is_reserved(struct page *page)
{
    return (page->flags & PG_RESERVED) != 0;
}

static inline void page_set_flag(struct page *page, u32 flag)
{
    page->flags |= flag;
}

static inline void page_clear_flag(struct page *page, u32 flag)
{
    page->flags &= ~flag;
}

/*
 * PMM API
 */

/* Initialization */
void pmm_init(void);

/* Page allocation */
struct page *alloc_pages(unsigned int order, unsigned int gfp_flags);
struct page *alloc_page(unsigned int gfp_flags);
void free_pages(struct page *page, unsigned int order);
void free_page(struct page *page);

/* Convenience wrappers that return virtual addresses */
void *get_free_pages(unsigned int order, unsigned int gfp_flags);
void *get_free_page(unsigned int gfp_flags);
void *get_zeroed_page(unsigned int gfp_flags);

/* Zone-specific allocation */
struct page *alloc_pages_zone(enum zone_type zone, unsigned int order,
                              unsigned int gfp_flags);

/* Memory reservation */
void pmm_reserve_range(phys_addr_t start, phys_addr_t end);
void pmm_free_range(phys_addr_t start, phys_addr_t end);

/* Statistics */
u64 pmm_get_free_pages(void);
u64 pmm_get_total_pages(void);
void pmm_dump_stats(void);

/* Per-CPU cache management */
void pmm_init_pcpu_cache(int cpu_id);
void pmm_drain_pcpu_cache(int cpu_id);

/* Debug */
void pmm_dump_free_areas(void);
void pmm_verify_integrity(void);

#endif /* _OCEAN_PMM_H */
