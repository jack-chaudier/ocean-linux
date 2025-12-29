/*
 * Ocean Kernel - Memory Bitmap
 *
 * Bitmap for tracking physical memory state at page granularity.
 * Used to track memory holes, reserved regions, and allocations.
 *
 * Each bit represents one physical page:
 *   0 = free/usable
 *   1 = allocated/reserved
 */

#include <ocean/pmm.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*
 * Memory bitmap state
 */
struct mem_bitmap {
    u64 *bits;              /* Bitmap array */
    u64 nr_bits;            /* Total bits (pages) tracked */
    u64 nr_words;           /* Number of 64-bit words */
    pfn_t base_pfn;         /* First PFN covered by bitmap */
    spinlock_t lock;
};

static struct mem_bitmap mem_bitmap;

/*
 * Bitmap manipulation helpers
 */

static inline u64 word_index(pfn_t pfn)
{
    return (pfn - mem_bitmap.base_pfn) / 64;
}

static inline u64 bit_index(pfn_t pfn)
{
    return (pfn - mem_bitmap.base_pfn) % 64;
}

static inline bool bitmap_test_bit(pfn_t pfn)
{
    return (mem_bitmap.bits[word_index(pfn)] >> bit_index(pfn)) & 1;
}

static inline void bitmap_set_bit(pfn_t pfn)
{
    mem_bitmap.bits[word_index(pfn)] |= (1ULL << bit_index(pfn));
}

static inline void bitmap_clear_bit(pfn_t pfn)
{
    mem_bitmap.bits[word_index(pfn)] &= ~(1ULL << bit_index(pfn));
}

/*
 * Set a range of bits (mark pages as used/reserved)
 */
static void bitmap_set_range(pfn_t start_pfn, u64 nr_pages)
{
    pfn_t end_pfn = start_pfn + nr_pages;

    for (pfn_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        if (pfn >= mem_bitmap.base_pfn &&
            pfn < mem_bitmap.base_pfn + mem_bitmap.nr_bits) {
            bitmap_set_bit(pfn);
        }
    }
}

/*
 * Clear a range of bits (mark pages as free)
 */
static void bitmap_clear_range(pfn_t start_pfn, u64 nr_pages)
{
    pfn_t end_pfn = start_pfn + nr_pages;

    for (pfn_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        if (pfn >= mem_bitmap.base_pfn &&
            pfn < mem_bitmap.base_pfn + mem_bitmap.nr_bits) {
            bitmap_clear_bit(pfn);
        }
    }
}

/*
 * Count free pages in bitmap
 */
static u64 bitmap_count_free(void)
{
    u64 count = 0;

    for (u64 i = 0; i < mem_bitmap.nr_words; i++) {
        /* Count zero bits using popcount of inverted value */
        u64 word = mem_bitmap.bits[i];
        /* Count set bits, then we need to subtract from 64 */
        /* Simple loop for now - could use __builtin_popcountll */
        u64 set_bits = 0;
        u64 tmp = word;
        while (tmp) {
            set_bits += tmp & 1;
            tmp >>= 1;
        }
        count += (64 - set_bits);
    }

    /* Adjust for any padding bits in the last word */
    u64 extra_bits = (mem_bitmap.nr_words * 64) - mem_bitmap.nr_bits;
    count -= extra_bits;

    return count;
}

/*
 * Find first free page (for simple allocation)
 */
static pfn_t bitmap_find_free(void)
{
    for (u64 i = 0; i < mem_bitmap.nr_words; i++) {
        if (mem_bitmap.bits[i] != ~0ULL) {
            /* Found a word with at least one free bit */
            u64 word = mem_bitmap.bits[i];
            for (int j = 0; j < 64; j++) {
                if (!(word & (1ULL << j))) {
                    pfn_t pfn = mem_bitmap.base_pfn + i * 64 + j;
                    if (pfn < mem_bitmap.base_pfn + mem_bitmap.nr_bits) {
                        return pfn;
                    }
                }
            }
        }
    }

    return (pfn_t)-1;  /* No free page found */
}

/*
 * Find a contiguous range of free pages
 */
static pfn_t bitmap_find_free_range(u64 nr_pages)
{
    pfn_t start = mem_bitmap.base_pfn;
    pfn_t end = mem_bitmap.base_pfn + mem_bitmap.nr_bits;
    u64 count = 0;
    pfn_t range_start = 0;

    for (pfn_t pfn = start; pfn < end; pfn++) {
        if (!bitmap_test_bit(pfn)) {
            if (count == 0) {
                range_start = pfn;
            }
            count++;
            if (count >= nr_pages) {
                return range_start;
            }
        } else {
            count = 0;
        }
    }

    return (pfn_t)-1;  /* No free range found */
}

/*
 * Initialize the memory bitmap
 *
 * Called during PMM initialization with the memory map from the bootloader.
 */
void mem_bitmap_init(pfn_t max_pfn, void *bitmap_memory)
{
    mem_bitmap.base_pfn = 0;
    mem_bitmap.nr_bits = max_pfn;
    mem_bitmap.nr_words = (max_pfn + 63) / 64;
    mem_bitmap.bits = (u64 *)bitmap_memory;
    spin_init(&mem_bitmap.lock);

    /* Mark all pages as used initially */
    memset(mem_bitmap.bits, 0xFF, mem_bitmap.nr_words * sizeof(u64));

    kprintf("Memory bitmap initialized: %llu pages (%llu KiB bitmap)\n",
            mem_bitmap.nr_bits, (mem_bitmap.nr_words * 8) / 1024);
}

/*
 * Mark a physical range as usable (free)
 */
void mem_bitmap_mark_usable(phys_addr_t start, phys_addr_t end)
{
    pfn_t start_pfn = PHYS_TO_PFN(PAGE_ALIGN(start));
    pfn_t end_pfn = PHYS_TO_PFN(PAGE_ALIGN_DOWN(end));
    u64 flags;

    if (end_pfn <= start_pfn) {
        return;
    }

    spin_lock_irqsave(&mem_bitmap.lock, &flags);
    bitmap_clear_range(start_pfn, end_pfn - start_pfn);
    spin_unlock_irqrestore(&mem_bitmap.lock, flags);
}

/*
 * Mark a physical range as reserved (used)
 */
void mem_bitmap_mark_reserved(phys_addr_t start, phys_addr_t end)
{
    pfn_t start_pfn = PHYS_TO_PFN(PAGE_ALIGN_DOWN(start));
    pfn_t end_pfn = PHYS_TO_PFN(PAGE_ALIGN(end));
    u64 flags;

    if (end_pfn <= start_pfn) {
        return;
    }

    spin_lock_irqsave(&mem_bitmap.lock, &flags);
    bitmap_set_range(start_pfn, end_pfn - start_pfn);
    spin_unlock_irqrestore(&mem_bitmap.lock, flags);
}

/*
 * Check if a page is usable
 */
bool mem_bitmap_is_usable(pfn_t pfn)
{
    if (pfn < mem_bitmap.base_pfn ||
        pfn >= mem_bitmap.base_pfn + mem_bitmap.nr_bits) {
        return false;
    }

    return !bitmap_test_bit(pfn);
}

/*
 * Allocate a single page from bitmap (fallback allocator)
 */
pfn_t mem_bitmap_alloc_page(void)
{
    u64 flags;
    pfn_t pfn;

    spin_lock_irqsave(&mem_bitmap.lock, &flags);

    pfn = bitmap_find_free();
    if (pfn != (pfn_t)-1) {
        bitmap_set_bit(pfn);
    }

    spin_unlock_irqrestore(&mem_bitmap.lock, flags);

    return pfn;
}

/*
 * Allocate a contiguous range of pages from bitmap
 */
pfn_t mem_bitmap_alloc_pages(u64 nr_pages)
{
    u64 flags;
    pfn_t pfn;

    spin_lock_irqsave(&mem_bitmap.lock, &flags);

    pfn = bitmap_find_free_range(nr_pages);
    if (pfn != (pfn_t)-1) {
        bitmap_set_range(pfn, nr_pages);
    }

    spin_unlock_irqrestore(&mem_bitmap.lock, flags);

    return pfn;
}

/*
 * Free a page back to bitmap
 */
void mem_bitmap_free_page(pfn_t pfn)
{
    u64 flags;

    if (pfn < mem_bitmap.base_pfn ||
        pfn >= mem_bitmap.base_pfn + mem_bitmap.nr_bits) {
        return;
    }

    spin_lock_irqsave(&mem_bitmap.lock, &flags);
    bitmap_clear_bit(pfn);
    spin_unlock_irqrestore(&mem_bitmap.lock, flags);
}

/*
 * Free a range of pages back to bitmap
 */
void mem_bitmap_free_pages(pfn_t start_pfn, u64 nr_pages)
{
    u64 flags;

    spin_lock_irqsave(&mem_bitmap.lock, &flags);
    bitmap_clear_range(start_pfn, nr_pages);
    spin_unlock_irqrestore(&mem_bitmap.lock, flags);
}

/*
 * Get the size of memory needed for the bitmap
 */
u64 mem_bitmap_size_for(pfn_t max_pfn)
{
    u64 nr_words = (max_pfn + 63) / 64;
    return nr_words * sizeof(u64);
}

/*
 * Dump bitmap statistics
 */
void mem_bitmap_dump_stats(void)
{
    u64 flags;

    spin_lock_irqsave(&mem_bitmap.lock, &flags);
    u64 free = bitmap_count_free();
    spin_unlock_irqrestore(&mem_bitmap.lock, flags);

    kprintf("Memory bitmap stats:\n");
    kprintf("  Total pages: %llu\n", mem_bitmap.nr_bits);
    kprintf("  Free pages:  %llu\n", free);
    kprintf("  Used pages:  %llu\n", mem_bitmap.nr_bits - free);
    kprintf("  Bitmap size: %llu bytes\n", mem_bitmap.nr_words * sizeof(u64));
}

/*
 * Dump a visual representation of memory (for debugging)
 *
 * Shows memory in 1 MiB chunks, with '.' for mostly free and '#' for mostly used
 */
void mem_bitmap_dump_visual(void)
{
    #define CHUNK_PAGES (256)  /* 1 MiB = 256 pages */

    kprintf("Memory map (each char = 1 MiB, '.' = free, '#' = used):\n");

    u64 total_chunks = mem_bitmap.nr_bits / CHUNK_PAGES;
    if (total_chunks > 256) {
        total_chunks = 256;  /* Limit output */
    }

    for (u64 chunk = 0; chunk < total_chunks; chunk++) {
        pfn_t start = mem_bitmap.base_pfn + chunk * CHUNK_PAGES;
        u64 used = 0;

        for (u64 i = 0; i < CHUNK_PAGES; i++) {
            if (bitmap_test_bit(start + i)) {
                used++;
            }
        }

        if (chunk % 64 == 0) {
            kprintf("\n  %4lluM: ", (chunk * CHUNK_PAGES * PAGE_SIZE) / (1024 * 1024));
        }

        if (used < CHUNK_PAGES / 4) {
            kprintf(".");
        } else if (used < CHUNK_PAGES * 3 / 4) {
            kprintf("o");
        } else {
            kprintf("#");
        }
    }

    kprintf("\n\n");

    #undef CHUNK_PAGES
}
