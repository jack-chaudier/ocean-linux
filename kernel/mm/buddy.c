/*
 * Ocean Kernel - Buddy Allocator
 *
 * Classic buddy system for physical page allocation.
 *
 * The buddy allocator maintains free lists for power-of-two sized blocks.
 * When allocating, it finds the smallest block that fits, splitting larger
 * blocks as needed. When freeing, it coalesces adjacent "buddy" blocks.
 *
 * Orders: 0 = 1 page (4K), 1 = 2 pages (8K), ..., 10 = 1024 pages (4M)
 */

#include <ocean/pmm.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*
 * Get the buddy PFN for a given PFN at a given order
 *
 * Buddies are found by XORing the appropriate bit in the PFN.
 * For example, at order 0, pages 0 and 1 are buddies, 2 and 3 are buddies, etc.
 * At order 1, pages 0-1 and 2-3 are buddies, etc.
 */
static inline pfn_t buddy_pfn(pfn_t pfn, unsigned int order)
{
    return pfn ^ (1UL << order);
}

/*
 * Get the combined PFN when merging two buddies
 * This is the lower of the two buddy PFNs
 */
static inline pfn_t combined_pfn(pfn_t pfn, unsigned int order)
{
    return pfn & ~(1UL << order);
}

/*
 * Check if a page is a valid buddy for coalescing
 *
 * A page is a valid buddy if:
 *   1. It's within the zone bounds
 *   2. It's free (in buddy system)
 *   3. It has the same order
 */
static bool page_is_buddy(struct zone *zone, struct page *page,
                          pfn_t buddy_pfn, unsigned int order)
{
    /* Check zone bounds */
    if (buddy_pfn < zone->start_pfn || buddy_pfn >= zone->end_pfn) {
        return false;
    }

    /* Check if page is in buddy system and has correct order */
    if (!(page->flags & PG_BUDDY)) {
        return false;
    }

    if (page->order != order) {
        return false;
    }

    return true;
}

/*
 * Add a page block to a free area
 */
static void add_to_free_area(struct page *page, struct free_area *area,
                             unsigned int order)
{
    list_add(&page->buddy_list, &area->free_list);
    area->nr_free++;
    page->order = order;
    page_set_flag(page, PG_BUDDY);
}

/*
 * Remove a page block from a free area
 */
static void remove_from_free_area(struct page *page, struct free_area *area)
{
    list_del(&page->buddy_list);
    area->nr_free--;
    page_clear_flag(page, PG_BUDDY);
}

/*
 * Expand a higher-order block to get the requested order
 *
 * Takes a block of order 'high' and splits it down to order 'low',
 * adding the unused buddy halves back to the free lists.
 */
static void expand(struct zone *zone, struct page *page,
                   unsigned int low, unsigned int high)
{
    unsigned int size = 1UL << high;

    while (high > low) {
        high--;
        size >>= 1;

        /* Add the upper buddy to the free list */
        struct page *buddy = page + size;
        add_to_free_area(buddy, &zone->free_area[high], high);
        zone->free_pages += (1UL << high);
    }
}

/*
 * Allocate pages from a specific zone
 *
 * Returns the first page of the allocated block, or NULL if allocation fails.
 */
struct page *buddy_alloc_pages(struct zone *zone, unsigned int order)
{
    struct page *page = NULL;
    unsigned int current_order;
    u64 flags;

    if (order >= MAX_ORDER) {
        return NULL;
    }

    spin_lock_irqsave(&zone->lock, &flags);

    /* Find a free block of sufficient size */
    for (current_order = order; current_order < MAX_ORDER; current_order++) {
        struct free_area *area = &zone->free_area[current_order];

        if (list_empty(&area->free_list)) {
            continue;
        }

        /* Found a block - remove it from the free list */
        page = list_first_entry(&area->free_list, struct page, buddy_list);
        remove_from_free_area(page, area);

        /* Split if necessary */
        if (current_order > order) {
            expand(zone, page, order, current_order);
        }

        /* Update statistics */
        zone->free_pages -= (1UL << order);
        zone->alloc_count++;

        break;
    }

    spin_unlock_irqrestore(&zone->lock, flags);

    return page;
}

/*
 * Free pages back to the buddy allocator
 *
 * Attempts to coalesce with buddy blocks to form larger free blocks.
 */
void buddy_free_pages(struct zone *zone, struct page *page, unsigned int order)
{
    pfn_t pfn = page_to_pfn(page);
    u64 flags;

    if (order >= MAX_ORDER) {
        kprintf("buddy_free: invalid order %u\n", order);
        return;
    }

    spin_lock_irqsave(&zone->lock, &flags);

    /* Try to coalesce with buddy */
    while (order < MAX_ORDER - 1) {
        pfn_t buddy_pfn_val = buddy_pfn(pfn, order);
        struct page *buddy = pfn_to_page(buddy_pfn_val);

        /* Check if buddy can be merged */
        if (!page_is_buddy(zone, buddy, buddy_pfn_val, order)) {
            break;
        }

        /* Remove buddy from its free list */
        remove_from_free_area(buddy, &zone->free_area[order]);

        /* Combine with buddy */
        pfn = combined_pfn(pfn, order);
        page = pfn_to_page(pfn);
        order++;
    }

    /* Add the (possibly coalesced) block to the free list */
    add_to_free_area(page, &zone->free_area[order], order);

    /* Update statistics */
    zone->free_pages += (1UL << order);
    zone->free_count++;

    spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Initialize free areas for a zone
 */
void buddy_init_zone(struct zone *zone)
{
    unsigned int order;

    for (order = 0; order < MAX_ORDER; order++) {
        INIT_LIST_HEAD(&zone->free_area[order].free_list);
        zone->free_area[order].nr_free = 0;
    }

    spin_init(&zone->lock);
}

/*
 * Add a range of pages to the buddy allocator
 *
 * Pages must be contiguous and aligned to the largest possible order.
 * This function is called during PMM initialization to add free memory.
 */
void buddy_add_pages(struct zone *zone, pfn_t start_pfn, u64 nr_pages)
{
    pfn_t pfn = start_pfn;
    pfn_t end_pfn = start_pfn + nr_pages;
    u64 flags;

    spin_lock_irqsave(&zone->lock, &flags);

    while (pfn < end_pfn) {
        unsigned int order;

        /* Find the largest order block we can add starting at this PFN */
        for (order = MAX_ORDER - 1; order > 0; order--) {
            u64 block_pages = 1UL << order;

            /* Block must be naturally aligned */
            if (pfn & (block_pages - 1)) {
                continue;
            }

            /* Block must fit in remaining pages */
            if (pfn + block_pages > end_pfn) {
                continue;
            }

            break;
        }

        /* Add this block to the free list */
        struct page *page = pfn_to_page(pfn);
        add_to_free_area(page, &zone->free_area[order], order);
        zone->free_pages += (1UL << order);

        pfn += (1UL << order);
    }

    spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Dump free area information for debugging
 */
void buddy_dump_free_areas(struct zone *zone)
{
    unsigned int order;

    kprintf("  Zone %s free areas:\n", zone->name);
    kprintf("  Order  Pages  Free Blocks\n");
    kprintf("  -----  -----  -----------\n");

    for (order = 0; order < MAX_ORDER; order++) {
        u64 pages = 1UL << order;
        kprintf("  %5u  %5llu  %llu\n",
                order, pages, zone->free_area[order].nr_free);
    }
}

/*
 * Verify buddy allocator integrity
 *
 * Checks that:
 *   1. All pages in free lists have PG_BUDDY flag set
 *   2. All pages have correct order
 *   3. Free list counts match nr_free
 */
void buddy_verify_integrity(struct zone *zone)
{
    unsigned int order;
    u64 flags;

    spin_lock_irqsave(&zone->lock, &flags);

    for (order = 0; order < MAX_ORDER; order++) {
        struct free_area *area = &zone->free_area[order];
        struct page *page;
        u64 count = 0;

        list_for_each_entry(page, &area->free_list, buddy_list) {
            count++;

            if (!(page->flags & PG_BUDDY)) {
                kprintf("INTEGRITY ERROR: page %p in free list order %u "
                        "missing PG_BUDDY flag\n", page, order);
            }

            if (page->order != order) {
                kprintf("INTEGRITY ERROR: page %p has order %u but in "
                        "order %u free list\n", page, page->order, order);
            }
        }

        if (count != area->nr_free) {
            kprintf("INTEGRITY ERROR: order %u free list count %llu != "
                    "nr_free %llu\n", order, count, area->nr_free);
        }
    }

    spin_unlock_irqrestore(&zone->lock, flags);
}
