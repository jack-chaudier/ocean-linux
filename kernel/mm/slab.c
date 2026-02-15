/*
 * Ocean Kernel - Slab Allocator
 *
 * A simple slab allocator for fixed-size kernel allocations.
 * Also provides kmalloc/kfree for general-purpose allocations
 * using size-based slab caches.
 */

#include <ocean/vmm.h>
#include <ocean/pmm.h>
#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/list.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/* PMM functions */
extern void *simple_get_free_page(void);
extern void simple_free_page(void *addr);
extern void *simple_get_free_pages(unsigned int order);
extern void simple_free_pages(void *addr, unsigned int order);
#define get_free_page() simple_get_free_page()
#define free_page(addr) simple_free_page(addr)
#define get_free_pages(order) simple_get_free_pages(order)
#define free_pages(addr, order) simple_free_pages(addr, order)

/*
 * Slab structure - placed at the beginning of each slab page
 */
struct slab {
    struct slab_cache *cache;   /* Owning cache */
    struct list_head list;      /* Link in cache's slab list */
    void *freelist;             /* List of free objects */
    u32 inuse;                  /* Number of objects in use */
    u32 free;                   /* Number of free objects */
    void *start;                /* Start of object area */
};

/* Global list of all slab caches */
static LIST_HEAD(cache_list);
static spinlock_t cache_list_lock;

/* Size classes for kmalloc (powers of 2 from 8 to 2048)
 * Objects > 2048 go directly to page allocator because the slab
 * header leaves insufficient space in a single 4KB page. */
#define KMALLOC_MIN_SIZE    8
#define KMALLOC_MAX_SIZE    2048
#define KMALLOC_NUM_CACHES  9   /* 8, 16, 32, 64, 128, 256, 512, 1024, 2048 */

static struct slab_cache *kmalloc_caches[KMALLOC_NUM_CACHES];
static bool slab_initialized = false;

static inline struct page *virt_to_page_meta(void *addr)
{
    if (!addr) {
        return NULL;
    }
    return phys_to_page(virt_to_phys(addr));
}

/*
 * Get the slab structure from an object pointer
 */
static inline struct slab *obj_to_slab(void *obj)
{
    /* Slab is at the start of the page containing the object */
    return (struct slab *)((u64)obj & ~(PAGE_SIZE - 1));
}

/*
 * Calculate number of objects that fit in a slab
 */
static u32 calc_obj_per_slab(size_t obj_size, size_t align)
{
    size_t slab_overhead = sizeof(struct slab);
    size_t usable = PAGE_SIZE - slab_overhead;

    /* Align object size */
    size_t aligned_size = (obj_size + align - 1) & ~(align - 1);
    if (aligned_size < sizeof(void *)) {
        aligned_size = sizeof(void *); /* Need space for freelist pointer */
    }

    return usable / aligned_size;
}

/*
 * Get aligned object size
 */
static size_t get_aligned_size(size_t obj_size, size_t align)
{
    size_t aligned_size = (obj_size + align - 1) & ~(align - 1);
    if (aligned_size < sizeof(void *)) {
        aligned_size = sizeof(void *);
    }
    return aligned_size;
}

/*
 * Allocate a new slab
 */
static struct slab *slab_alloc_new(struct slab_cache *cache)
{
    void *page = get_free_page();
    if (!page) {
        return NULL;
    }

    struct slab *slab = (struct slab *)page;
    slab->cache = cache;
    INIT_LIST_HEAD(&slab->list);
    slab->inuse = 0;
    slab->free = cache->obj_per_slab;
    slab->start = (void *)((u64)page + sizeof(struct slab));

    /* Build freelist */
    size_t aligned_size = get_aligned_size(cache->obj_size, cache->align);
    void **freelist = &slab->freelist;

    u8 *obj = (u8 *)slab->start;
    for (u32 i = 0; i < cache->obj_per_slab; i++) {
        *freelist = obj;
        freelist = (void **)obj;
        obj += aligned_size;
    }
    *freelist = NULL;

    struct page *meta = virt_to_page_meta(page);
    if (meta) {
        page_set_flag(meta, PG_SLAB);
    }

    cache->total_slabs++;

    return slab;
}

/*
 * Free a slab back to the page allocator
 */
static void slab_free_slab(struct slab *slab)
{
    struct page *meta = virt_to_page_meta(slab);
    if (meta) {
        page_clear_flag(meta, PG_SLAB);
    }
    slab->cache->total_slabs--;
    free_page(slab);
}

/*
 * Create a new slab cache
 */
struct slab_cache *slab_cache_create(const char *name, size_t size, size_t align)
{
    /* For now, use a page to hold the cache structure */
    void *page = get_free_page();
    if (!page) {
        return NULL;
    }
    struct slab_cache *cache = (struct slab_cache *)page;

    memset(cache, 0, sizeof(*cache));

    cache->name = name;
    cache->obj_size = size;
    cache->align = align ? align : sizeof(void *);
    cache->obj_per_slab = calc_obj_per_slab(size, cache->align);

    INIT_LIST_HEAD(&cache->slabs_full);
    INIT_LIST_HEAD(&cache->slabs_partial);
    INIT_LIST_HEAD(&cache->slabs_free);
    INIT_LIST_HEAD(&cache->cache_list);

    spin_init(&cache->lock);

    /* Add to global cache list */
    u64 flags;
    spin_lock_irqsave(&cache_list_lock, &flags);
    list_add(&cache->cache_list, &cache_list);
    spin_unlock_irqrestore(&cache_list_lock, flags);

    return cache;
}

/*
 * Destroy a slab cache
 */
void slab_cache_destroy(struct slab_cache *cache)
{
    if (!cache) return;

    /* Remove from global list */
    u64 flags;
    spin_lock_irqsave(&cache_list_lock, &flags);
    list_del(&cache->cache_list);
    spin_unlock_irqrestore(&cache_list_lock, flags);

    /* Free all slabs */
    struct slab *slab, *tmp;

    list_for_each_entry_safe(slab, tmp, &cache->slabs_full, list) {
        list_del(&slab->list);
        slab_free_slab(slab);
    }

    list_for_each_entry_safe(slab, tmp, &cache->slabs_partial, list) {
        list_del(&slab->list);
        slab_free_slab(slab);
    }

    list_for_each_entry_safe(slab, tmp, &cache->slabs_free, list) {
        list_del(&slab->list);
        slab_free_slab(slab);
    }

    free_page(cache);
}

/*
 * Allocate an object from a slab cache
 */
void *slab_alloc(struct slab_cache *cache)
{
    u64 flags;
    void *obj = NULL;

    spin_lock_irqsave(&cache->lock, &flags);

    struct slab *slab = NULL;

    /* Try partial slabs first */
    if (!list_empty(&cache->slabs_partial)) {
        slab = list_first_entry(&cache->slabs_partial, struct slab, list);
    }
    /* Then try free slabs */
    else if (!list_empty(&cache->slabs_free)) {
        slab = list_first_entry(&cache->slabs_free, struct slab, list);
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_partial);
    }
    /* Need to allocate a new slab */
    else {
        spin_unlock_irqrestore(&cache->lock, flags);
        slab = slab_alloc_new(cache);
        if (!slab) {
            return NULL;
        }
        spin_lock_irqsave(&cache->lock, &flags);
        list_add(&slab->list, &cache->slabs_partial);
    }

    /* Get object from slab's freelist */
    obj = slab->freelist;
    slab->freelist = *(void **)obj;
    slab->inuse++;
    slab->free--;

    /* Move slab to full list if needed */
    if (slab->free == 0) {
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_full);
    }

    cache->total_allocs++;
    cache->active_objs++;

    spin_unlock_irqrestore(&cache->lock, flags);

    return obj;
}

/*
 * Free an object back to its slab cache
 */
void slab_free(struct slab_cache *cache, void *obj)
{
    if (!obj) return;

    struct slab *slab = obj_to_slab(obj);

    /* Verify slab belongs to this cache */
    if (slab->cache != cache) {
        kprintf("slab_free: object %p doesn't belong to cache %s\n",
                obj, cache->name);
        return;
    }

    u64 flags;
    spin_lock_irqsave(&cache->lock, &flags);

    /* Add to freelist */
    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    slab->inuse--;
    slab->free++;

    /* Move slab between lists as needed */
    if (slab->inuse == 0) {
        /* Slab is now empty */
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_free);
    } else if (slab->free == 1) {
        /* Was full, now partial */
        list_del(&slab->list);
        list_add(&slab->list, &cache->slabs_partial);
    }

    cache->total_frees++;
    cache->active_objs--;

    spin_unlock_irqrestore(&cache->lock, flags);
}

/*
 * Shrink a slab cache (free empty slabs)
 */
void slab_cache_shrink(struct slab_cache *cache)
{
    u64 flags;
    spin_lock_irqsave(&cache->lock, &flags);

    struct slab *slab, *tmp;
    list_for_each_entry_safe(slab, tmp, &cache->slabs_free, list) {
        list_del(&slab->list);
        slab_free_slab(slab);
    }

    spin_unlock_irqrestore(&cache->lock, flags);
}

/*
 * Dump slab cache statistics
 */
void slab_cache_dump(struct slab_cache *cache)
{
    kprintf("Slab cache '%s':\n", cache->name);
    kprintf("  Object size: %zu, Align: %zu\n", cache->obj_size, cache->align);
    kprintf("  Objects per slab: %u\n", cache->obj_per_slab);
    kprintf("  Total slabs: %llu\n", cache->total_slabs);
    kprintf("  Active objects: %llu\n", cache->active_objs);
    kprintf("  Total allocs: %llu, frees: %llu\n",
            cache->total_allocs, cache->total_frees);
}

/*
 * Initialize kmalloc caches
 */
static void kmalloc_init(void)
{
    static const char *names[] = {
        "kmalloc-8", "kmalloc-16", "kmalloc-32", "kmalloc-64",
        "kmalloc-128", "kmalloc-256", "kmalloc-512", "kmalloc-1024",
        "kmalloc-2048"
    };

    size_t size = KMALLOC_MIN_SIZE;
    for (int i = 0; i < KMALLOC_NUM_CACHES; i++) {
        kmalloc_caches[i] = slab_cache_create(names[i], size, size);
        if (!kmalloc_caches[i]) {
            kprintf("Failed to create kmalloc cache for size %zu\n", size);
        }
        size *= 2;
    }
}

/*
 * Get kmalloc cache for a given size
 */
static struct slab_cache *kmalloc_cache_for_size(size_t size)
{
    if (size <= 8) return kmalloc_caches[0];
    if (size <= 16) return kmalloc_caches[1];
    if (size <= 32) return kmalloc_caches[2];
    if (size <= 64) return kmalloc_caches[3];
    if (size <= 128) return kmalloc_caches[4];
    if (size <= 256) return kmalloc_caches[5];
    if (size <= 512) return kmalloc_caches[6];
    if (size <= 1024) return kmalloc_caches[7];
    if (size <= 2048) return kmalloc_caches[8];
    return NULL;  /* Larger sizes use page allocator directly */
}

/*
 * Initialize kernel heap
 */
void kheap_init(void)
{
    kprintf("Initializing kernel heap (slab allocator)...\n");

    spin_init(&cache_list_lock);
    kmalloc_init();

    slab_initialized = true;

    kprintf("  Kernel heap ready (%d size classes)\n", KMALLOC_NUM_CACHES);
}

/*
 * Allocate kernel memory
 */
void *kmalloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    /* For large allocations, use page allocator directly */
    if (size > KMALLOC_MAX_SIZE) {
        unsigned int order = 0;
        size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        while ((1U << order) < pages) {
            order++;
        }
        return get_free_pages(order);
    }

    /* Use slab allocator */
    struct slab_cache *cache = kmalloc_cache_for_size(size);
    if (!cache) {
        return NULL;
    }

    return slab_alloc(cache);
}

/*
 * Allocate zeroed kernel memory
 */
void *kzalloc(size_t size)
{
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Free kernel memory
 */
void kfree(void *ptr)
{
    if (!ptr) return;

    struct page *meta = virt_to_page_meta(ptr);
    if (!meta) {
        return;
    }

    if (meta->flags & PG_SLAB) {
        struct slab *slab = obj_to_slab(ptr);
        if (slab->cache) {
            slab_free(slab->cache, ptr);
        }
        return;
    }

    /* Large/page allocations are always page-aligned. */
    if (((u64)ptr & (PAGE_SIZE - 1)) != 0) {
        kprintf("kfree: non-slab pointer %p is not page-aligned\n", ptr);
        return;
    }

    if ((meta->flags & PG_HEAD) && (meta->flags & PG_COMPOUND)) {
        free_pages(ptr, meta->order);
    } else {
        free_page(ptr);
    }
}

/*
 * Allocate aligned memory
 */
void *kmalloc_aligned(size_t size, size_t align)
{
    /* For now, use next power of 2 size that satisfies alignment */
    size_t alloc_size = size;
    if (align > sizeof(void *)) {
        alloc_size = (size + align - 1) & ~(align - 1);
    }
    return kmalloc(alloc_size);
}

/*
 * Get allocation size (approximate)
 */
size_t ksize(void *ptr)
{
    if (!ptr) return 0;

    struct page *meta = virt_to_page_meta(ptr);
    if (meta && (meta->flags & PG_SLAB)) {
        struct slab *slab = obj_to_slab(ptr);
        return slab->cache->obj_size;
    }

    if (meta && (meta->flags & PG_HEAD) && (meta->flags & PG_COMPOUND)) {
        return (size_t)(1UL << meta->order) * PAGE_SIZE;
    }

    return PAGE_SIZE;
}

/*
 * Dump kernel heap statistics
 */
void kheap_dump_stats(void)
{
    kprintf("\nKernel Heap Statistics:\n");

    u64 total_active = 0;
    u64 total_slabs = 0;

    for (int i = 0; i < KMALLOC_NUM_CACHES; i++) {
        if (kmalloc_caches[i]) {
            struct slab_cache *cache = kmalloc_caches[i];
            kprintf("  %s: %llu active, %llu slabs\n",
                    cache->name, cache->active_objs, cache->total_slabs);
            total_active += cache->active_objs;
            total_slabs += cache->total_slabs;
        }
    }

    kprintf("  Total: %llu active objects, %llu slabs (%llu KiB)\n",
            total_active, total_slabs, (total_slabs * PAGE_SIZE) / 1024);
}
