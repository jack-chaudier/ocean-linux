/*
 * Ocean Kernel - Virtual Memory Manager
 *
 * Address space management, VMA handling, and memory mapping.
 */

#include <ocean/vmm.h>
#include <ocean/pmm.h>
#include <ocean/boot.h>
#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/list.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/* PMM functions */
extern void *simple_get_free_page(void);
extern void simple_free_page(void *addr);
extern void *simple_get_free_pages(unsigned int order);
extern void simple_free_pages(void *addr, unsigned int order);
#define get_free_page() simple_get_free_page()
#define free_page(addr) simple_free_page(addr)
#define get_free_pages(order) simple_get_free_pages(order)
#define free_pages(addr, order) simple_free_pages(addr, order)

/* Paging functions */
extern void paging_init(void);
extern void kernel_space_init(void);

/* Slab caches for VMM structures */
static struct slab_cache *vma_cache;
static struct slab_cache *address_space_cache;

/* Kernel heap state */
static struct {
    u64 base;           /* Virtual base of kernel heap */
    u64 current;        /* Current allocation pointer */
    u64 end;            /* End of kernel heap */
    spinlock_t lock;
} kheap;

/*
 * Initialize VMM subsystem
 */
void vmm_init(void)
{
    kprintf("Initializing Virtual Memory Manager...\n");

    /* Initialize paging subsystem */
    paging_init();

    /* Initialize kernel address space */
    kernel_space_init();

    /* Initialize kernel heap */
    kheap_init();

    kprintf("VMM initialized\n");
}

/*
 * Create a new VMA
 */
static struct vm_area *vma_alloc(void)
{
    struct vm_area *vma;

    if (vma_cache) {
        vma = slab_alloc(vma_cache);
    } else {
        /* Early allocation before slab is ready */
        vma = get_free_page();
    }

    if (vma) {
        memset(vma, 0, sizeof(*vma));
        INIT_LIST_HEAD(&vma->list);
    }

    return vma;
}

/*
 * Free a VMA
 */
static void vma_free(struct vm_area *vma)
{
    if (vma_cache) {
        slab_free(vma_cache, vma);
    } else {
        free_page(vma);
    }
}

/*
 * Convert VMA flags to PTE flags
 */
static u64 vma_to_pte_flags(u32 vma_flags)
{
    u64 pte_flags = PTE_PRESENT;

    if (vma_flags & VMA_WRITE) {
        pte_flags |= PTE_WRITABLE;
    }
    if (!(vma_flags & VMA_EXEC)) {
        pte_flags |= PTE_NX;
    }
    /* User-space VMAs get PTE_USER */
    pte_flags |= PTE_USER;

    return pte_flags;
}

/*
 * Find VMA containing an address
 */
struct vm_area *vmm_find_vma(struct address_space *as, u64 addr)
{
    struct vm_area *vma;

    list_for_each_entry(vma, &as->vma_list, list) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        /* VMAs are sorted, so if we've passed it, stop */
        if (addr < vma->start) {
            break;
        }
    }

    return NULL;
}

/*
 * Find VMA that intersects with a range
 */
static struct vm_area *vma_find_intersect(struct address_space *as,
                                          u64 start, u64 end)
{
    struct vm_area *vma;

    list_for_each_entry(vma, &as->vma_list, list) {
        if (vma->start < end && vma->end > start) {
            return vma;
        }
        if (vma->start >= end) {
            break;
        }
    }

    return NULL;
}

/*
 * Insert VMA in sorted order
 */
static void vma_insert(struct address_space *as, struct vm_area *new_vma)
{
    struct vm_area *vma;
    struct list_head *pos = &as->vma_list;

    list_for_each_entry(vma, &as->vma_list, list) {
        if (new_vma->start < vma->start) {
            pos = &vma->list;
            break;
        }
    }

    list_add_tail(&new_vma->list, pos);
    as->vma_count++;
}

/*
 * Create a new address space
 */
struct address_space *vmm_create_address_space(void)
{
    struct address_space *as;

    if (address_space_cache) {
        as = slab_alloc(address_space_cache);
    } else {
        as = get_free_page();
    }

    if (!as) {
        return NULL;
    }

    memset(as, 0, sizeof(*as));

    /* Create new PML4 with kernel mappings */
    as->pml4 = paging_create_pml4();
    if (!as->pml4) {
        if (address_space_cache) {
            slab_free(address_space_cache, as);
        } else {
            free_page(as);
        }
        return NULL;
    }

    /* Get physical address of PML4 */
    const struct boot_info *boot = get_boot_info();
    as->pml4_phys = (phys_addr_t)as->pml4 - boot->hhdm_offset;

    INIT_LIST_HEAD(&as->vma_list);
    as->vma_count = 0;
    as->ref_count = 1;
    spin_init(&as->lock);

    return as;
}

/*
 * Destroy an address space
 */
void vmm_destroy_address_space(struct address_space *as)
{
    if (!as) return;

    as->ref_count--;
    if (as->ref_count > 0) {
        return;
    }

    /* Free all VMAs */
    struct vm_area *vma, *tmp;
    list_for_each_entry_safe(vma, tmp, &as->vma_list, list) {
        /* Free the physical pages in this VMA */
        for (u64 addr = vma->start; addr < vma->end; addr += PAGE_SIZE) {
            pte_t *pte = paging_get_pte(as->pml4, addr);
            if (pte && (*pte & PTE_PRESENT)) {
                phys_addr_t phys = *pte & PTE_ADDR_MASK;
                const struct boot_info *boot = get_boot_info();
                free_page((void *)(phys + boot->hhdm_offset));
            }
        }

        list_del(&vma->list);
        vma_free(vma);
    }

    /* Destroy page tables */
    paging_destroy_pml4(as->pml4);

    /* Free the address space structure */
    if (address_space_cache) {
        slab_free(address_space_cache, as);
    } else {
        free_page(as);
    }
}

/*
 * Map a region in an address space
 */
int vmm_map_region(struct address_space *as, u64 start, u64 size, u32 flags)
{
    u64 end = start + size;

    /* Check for overlap with existing VMAs */
    if (vma_find_intersect(as, start, end)) {
        return -1;
    }

    /* Create new VMA */
    struct vm_area *vma = vma_alloc();
    if (!vma) {
        return -1;
    }

    vma->start = start;
    vma->end = end;
    vma->flags = flags;
    vma->page_prot = vma_to_pte_flags(flags);

    /* Insert VMA */
    vma_insert(as, vma);

    /* Allocate and map physical pages */
    u64 pte_flags = vma_to_pte_flags(flags);
    for (u64 addr = start; addr < end; addr += PAGE_SIZE) {
        void *page = get_free_page();
        if (!page) {
            /* Rollback */
            vmm_unmap_region(as, start, addr - start);
            return -1;
        }

        const struct boot_info *boot = get_boot_info();
        phys_addr_t phys = (phys_addr_t)page - boot->hhdm_offset;

        if (paging_map(as->pml4, addr, phys, pte_flags) != 0) {
            free_page(page);
            vmm_unmap_region(as, start, addr - start);
            return -1;
        }

        /* Zero the page for security */
        memset(page, 0, PAGE_SIZE);
    }

    as->total_vm += size / PAGE_SIZE;

    return 0;
}

/*
 * Unmap a region
 */
int vmm_unmap_region(struct address_space *as, u64 start, u64 size)
{
    u64 end = start + size;

    /* Find and remove affected VMAs */
    struct vm_area *vma, *tmp;
    list_for_each_entry_safe(vma, tmp, &as->vma_list, list) {
        if (vma->start >= end) {
            break;
        }

        if (vma->end <= start) {
            continue;
        }

        /* VMA overlaps with region to unmap */
        u64 unmap_start = (vma->start > start) ? vma->start : start;
        u64 unmap_end = (vma->end < end) ? vma->end : end;

        /* Free physical pages and unmap */
        for (u64 addr = unmap_start; addr < unmap_end; addr += PAGE_SIZE) {
            pte_t *pte = paging_get_pte(as->pml4, addr);
            if (pte && (*pte & PTE_PRESENT)) {
                phys_addr_t phys = *pte & PTE_ADDR_MASK;
                const struct boot_info *boot = get_boot_info();
                free_page((void *)(phys + boot->hhdm_offset));
            }
            paging_unmap(as->pml4, addr);
        }

        /* Handle partial VMA removal */
        if (unmap_start == vma->start && unmap_end == vma->end) {
            /* Remove entire VMA */
            list_del(&vma->list);
            as->vma_count--;
            vma_free(vma);
        } else if (unmap_start == vma->start) {
            /* Remove start of VMA */
            vma->start = unmap_end;
        } else if (unmap_end == vma->end) {
            /* Remove end of VMA */
            vma->end = unmap_start;
        } else {
            /* Split VMA */
            struct vm_area *new_vma = vma_alloc();
            if (new_vma) {
                new_vma->start = unmap_end;
                new_vma->end = vma->end;
                new_vma->flags = vma->flags;
                new_vma->page_prot = vma->page_prot;
                vma->end = unmap_start;
                list_add(&new_vma->list, &vma->list);
                as->vma_count++;
            }
        }
    }

    as->total_vm -= size / PAGE_SIZE;

    return 0;
}

/*
 * Map physical memory into user space
 */
int vmm_map_to_user(struct address_space *as, u64 virt, phys_addr_t phys,
                    u64 size, u32 flags)
{
    u64 end = virt + size;

    /* Check for overlap */
    if (vma_find_intersect(as, virt, end)) {
        return -1;
    }

    /* Create VMA */
    struct vm_area *vma = vma_alloc();
    if (!vma) {
        return -1;
    }

    vma->start = virt;
    vma->end = end;
    vma->flags = flags;
    vma->page_prot = vma_to_pte_flags(flags);

    vma_insert(as, vma);

    /* Map the physical pages */
    u64 pte_flags = vma_to_pte_flags(flags);
    if (paging_map_range(as->pml4, virt, phys, size, pte_flags) != 0) {
        list_del(&vma->list);
        as->vma_count--;
        vma_free(vma);
        return -1;
    }

    as->total_vm += size / PAGE_SIZE;

    return 0;
}

/*
 * Simple mmap implementation
 */
u64 vmm_mmap(struct address_space *as, u64 hint, u64 size, u32 prot, u32 flags)
{
    /* Page-align size */
    size = PAGE_ALIGN(size);

    u64 addr = hint;

    /* Find free region if no hint or hint conflicts */
    if (addr == 0 || vma_find_intersect(as, addr, addr + size)) {
        /* Search for free space */
        addr = 0x10000000; /* Start at 256MB */
        while (addr + size < USER_SPACE_END) {
            if (!vma_find_intersect(as, addr, addr + size)) {
                break;
            }
            addr += PAGE_SIZE * 256; /* Skip in larger chunks */
        }

        if (addr + size >= USER_SPACE_END) {
            return (u64)-1; /* No space found */
        }
    }

    u32 vma_flags = 0;
    if (prot & VMA_READ) vma_flags |= VMA_READ;
    if (prot & VMA_WRITE) vma_flags |= VMA_WRITE;
    if (prot & VMA_EXEC) vma_flags |= VMA_EXEC;
    if (flags & VMA_ANONYMOUS) vma_flags |= VMA_ANONYMOUS;

    if (vmm_map_region(as, addr, size, vma_flags) != 0) {
        return (u64)-1;
    }

    return addr;
}

/*
 * munmap implementation
 */
int vmm_munmap(struct address_space *as, u64 addr, u64 size)
{
    return vmm_unmap_region(as, addr, size);
}

/*
 * Change page protection
 */
int vmm_mprotect(struct address_space *as, u64 addr, u64 size, u32 prot)
{
    u64 end = addr + size;

    struct vm_area *vma = vmm_find_vma(as, addr);
    if (!vma || vma->start > addr || vma->end < end) {
        return -1; /* Region not fully covered by a VMA */
    }

    /* Update VMA flags */
    vma->flags = (vma->flags & ~(VMA_READ | VMA_WRITE | VMA_EXEC)) | prot;
    u64 new_pte_flags = vma_to_pte_flags(vma->flags);
    vma->page_prot = new_pte_flags;

    /* Update page table entries */
    for (u64 a = addr; a < end; a += PAGE_SIZE) {
        pte_t *pte = paging_get_pte(as->pml4, a);
        if (pte && (*pte & PTE_PRESENT)) {
            phys_addr_t phys = *pte & PTE_ADDR_MASK;
            *pte = phys | new_pte_flags;
            tlb_flush_page(a);
        }
    }

    return 0;
}

/*
 * Clone an address space (for fork)
 * Uses copy-on-write for efficiency
 */
struct address_space *vmm_clone_address_space(struct address_space *src)
{
    struct address_space *dst = vmm_create_address_space();
    if (!dst) {
        return NULL;
    }

    /* Clone all VMAs */
    struct vm_area *vma;
    list_for_each_entry(vma, &src->vma_list, list) {
        struct vm_area *new_vma = vma_alloc();
        if (!new_vma) {
            vmm_destroy_address_space(dst);
            return NULL;
        }

        *new_vma = *vma;
        INIT_LIST_HEAD(&new_vma->list);

        /* For each page in the VMA, set up COW */
        for (u64 addr = vma->start; addr < vma->end; addr += PAGE_SIZE) {
            pte_t *src_pte = paging_get_pte(src->pml4, addr);
            if (!src_pte || !(*src_pte & PTE_PRESENT)) {
                continue;
            }

            phys_addr_t phys = *src_pte & PTE_ADDR_MASK;

            /* Mark both pages as COW (read-only + COW flag) */
            *src_pte = (*src_pte & ~PTE_WRITABLE) | PTE_COW;
            tlb_flush_page(addr);

            /* Map same physical page in child */
            u64 flags = (*src_pte & ~PTE_ADDR_MASK);
            paging_map(dst->pml4, addr, phys, flags);

            /* Increment page reference count */
            /* TODO: track page refcounts in struct page */
        }

        vma_insert(dst, new_vma);
    }

    dst->brk = src->brk;
    dst->start_brk = src->start_brk;
    dst->start_stack = src->start_stack;
    dst->total_vm = src->total_vm;

    return dst;
}
