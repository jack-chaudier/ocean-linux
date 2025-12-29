/*
 * Ocean Kernel - Virtual Memory Manager
 *
 * x86_64 4-level paging with higher-half kernel mapping.
 * Provides address space management, page fault handling,
 * and kernel heap services.
 */

#ifndef _OCEAN_VMM_H
#define _OCEAN_VMM_H

#include <ocean/types.h>
#include <ocean/defs.h>
#include <ocean/list.h>
#include <ocean/spinlock.h>

/* Forward declaration */
struct page;

/*
 * x86_64 Page Table Entry Flags
 */
#define PTE_PRESENT     (1ULL << 0)   /* Page is present in memory */
#define PTE_WRITABLE    (1ULL << 1)   /* Page is writable */
#define PTE_USER        (1ULL << 2)   /* Page accessible from user mode */
#define PTE_PWT         (1ULL << 3)   /* Page-level write-through */
#define PTE_PCD         (1ULL << 4)   /* Page-level cache disable */
#define PTE_ACCESSED    (1ULL << 5)   /* Page has been accessed */
#define PTE_DIRTY       (1ULL << 6)   /* Page has been written */
#define PTE_HUGE        (1ULL << 7)   /* Huge page (2MB/1GB) */
#define PTE_GLOBAL      (1ULL << 8)   /* Global page (not flushed on CR3 switch) */
#define PTE_NX          (1ULL << 63)  /* No-execute bit */

/* Custom software bits (available bits 9-11, 52-62) */
#define PTE_COW         (1ULL << 9)   /* Copy-on-write page */
#define PTE_SWAP        (1ULL << 10)  /* Page is swapped out */

/* Page table address mask (bits 12-51 for physical address) */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

/* Page table index masks */
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)

/* Number of entries per page table level */
#define PT_ENTRIES      512

/*
 * Virtual Address Space Layout (from plan)
 *
 * USER SPACE: 0x0000000000000000 - 0x00007FFFFFFFFFFF
 * KERNEL SPACE: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
 */

/* Kernel space regions */
#define KERNEL_SPACE_START      0xFFFF800000000000ULL
#define HHDM_BASE               0xFFFF800000000000ULL  /* Direct physical mapping */
#define HHDM_SIZE               0x0000080000000000ULL  /* 512 GiB */
#define KERNEL_HEAP_BASE        0xFFFF880000000000ULL  /* Kernel heap (vmalloc) */
#define KERNEL_HEAP_SIZE        0x0000080000000000ULL  /* 512 GiB */
#define KERNEL_STACK_BASE       0xFFFFE00000000000ULL  /* Kernel stacks */
#define KERNEL_STACK_REGION     0x0000100000000000ULL  /* 512 GiB region */
#define KERNEL_TEXT_BASE        0xFFFFFFFF80000000ULL  /* Kernel text/data */

/* User space regions */
#define USER_SPACE_START        0x0000000000000000ULL
#define USER_SPACE_END          0x00007FFFFFFFFFFFULL
#define USER_STACK_TOP          0x00007FFFFFFFE000ULL  /* Leave guard page */

/* Kernel stack size */
#define KERNEL_STACK_PAGES      4
#define KERNEL_STACK_SIZE_BYTES (KERNEL_STACK_PAGES * PAGE_SIZE)

/*
 * Page table types
 */
typedef u64 pte_t;      /* Page table entry */
typedef u64 pde_t;      /* Page directory entry */
typedef u64 pdpe_t;     /* Page directory pointer entry */
typedef u64 pml4e_t;    /* PML4 entry */

/*
 * Virtual Memory Area (VMA) - describes a contiguous region
 * in a process's address space
 */
#define VMA_READ        (1 << 0)
#define VMA_WRITE       (1 << 1)
#define VMA_EXEC        (1 << 2)
#define VMA_SHARED      (1 << 3)
#define VMA_STACK       (1 << 4)
#define VMA_HEAP        (1 << 5)
#define VMA_ANONYMOUS   (1 << 6)
#define VMA_FILE        (1 << 7)

struct vm_area {
    u64 start;                  /* Start virtual address */
    u64 end;                    /* End virtual address (exclusive) */
    u32 flags;                  /* VMA_* flags */
    u32 page_prot;              /* Page protection (PTE flags) */

    /* For file-backed mappings */
    void *file;                 /* File object (future) */
    u64 file_offset;            /* Offset in file */

    struct list_head list;      /* List of VMAs in address space */
};

/*
 * Address Space - represents a process's virtual memory
 */
struct address_space {
    pml4e_t *pml4;              /* Top-level page table (physical) */
    phys_addr_t pml4_phys;      /* Physical address of PML4 */

    struct list_head vma_list;  /* List of VMAs */
    u64 vma_count;              /* Number of VMAs */

    u64 brk;                    /* Current program break (heap end) */
    u64 start_brk;              /* Initial program break */
    u64 start_stack;            /* Stack start */

    u64 total_vm;               /* Total pages mapped */
    u64 shared_vm;              /* Shared pages */

    spinlock_t lock;            /* Protects this structure */
    u32 ref_count;              /* Reference count */
};

/*
 * Kernel address space (shared by all processes)
 */
extern struct address_space kernel_space;

/*
 * Page Table Functions
 */

/* Initialize paging subsystem */
void paging_init(void);

/* Create a new PML4 for a process */
pml4e_t *paging_create_pml4(void);

/* Free a PML4 and all associated page tables */
void paging_destroy_pml4(pml4e_t *pml4);

/* Map a virtual address to a physical address */
int paging_map(pml4e_t *pml4, u64 virt, phys_addr_t phys, u64 flags);

/* Unmap a virtual address */
int paging_unmap(pml4e_t *pml4, u64 virt);

/* Map a range of pages */
int paging_map_range(pml4e_t *pml4, u64 virt, phys_addr_t phys,
                     u64 size, u64 flags);

/* Unmap a range of pages */
int paging_unmap_range(pml4e_t *pml4, u64 virt, u64 size);

/* Get the physical address for a virtual address */
phys_addr_t paging_get_phys(pml4e_t *pml4, u64 virt);

/* Get the PTE for a virtual address (returns NULL if not mapped) */
pte_t *paging_get_pte(pml4e_t *pml4, u64 virt);

/* Switch to a different address space */
void paging_switch(struct address_space *as);

/* Flush TLB for a single page */
static inline void tlb_flush_page(u64 addr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Flush entire TLB */
static inline void tlb_flush_all(void)
{
    u64 cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/* CR register functions are defined in defs.h:
 * read_cr2(), read_cr3(), write_cr3()
 */

/*
 * VMM Functions
 */

/* Initialize VMM subsystem */
void vmm_init(void);

/* Create a new address space */
struct address_space *vmm_create_address_space(void);

/* Clone an address space (for fork) */
struct address_space *vmm_clone_address_space(struct address_space *src);

/* Destroy an address space */
void vmm_destroy_address_space(struct address_space *as);

/* Find VMA containing an address */
struct vm_area *vmm_find_vma(struct address_space *as, u64 addr);

/* Map a region in an address space */
int vmm_map_region(struct address_space *as, u64 start, u64 size, u32 flags);

/* Unmap a region */
int vmm_unmap_region(struct address_space *as, u64 start, u64 size);

/* Map kernel memory into user space */
int vmm_map_to_user(struct address_space *as, u64 virt, phys_addr_t phys,
                    u64 size, u32 flags);

/* Allocate virtual memory (mmap-like) */
u64 vmm_mmap(struct address_space *as, u64 hint, u64 size, u32 prot, u32 flags);

/* Free virtual memory (munmap-like) */
int vmm_munmap(struct address_space *as, u64 addr, u64 size);

/* Change page protection */
int vmm_mprotect(struct address_space *as, u64 addr, u64 size, u32 prot);

/*
 * Page Fault Handler
 */

/* Page fault error codes */
#define PF_PRESENT      (1 << 0)  /* Page was present */
#define PF_WRITE        (1 << 1)  /* Write access */
#define PF_USER         (1 << 2)  /* User mode access */
#define PF_RESERVED     (1 << 3)  /* Reserved bit set in PTE */
#define PF_INSTR        (1 << 4)  /* Instruction fetch */

/* Handle a page fault */
int vmm_page_fault(u64 fault_addr, u64 error_code);

/*
 * Kernel Heap (kmalloc/kfree)
 */

/* Initialize kernel heap */
void kheap_init(void);

/* Allocate kernel memory */
void *kmalloc(size_t size);

/* Allocate zeroed kernel memory */
void *kzalloc(size_t size);

/* Free kernel memory */
void kfree(void *ptr);

/* Allocate aligned memory */
void *kmalloc_aligned(size_t size, size_t align);

/* Get allocation size */
size_t ksize(void *ptr);

/* Kernel heap statistics */
void kheap_dump_stats(void);

/*
 * Slab Allocator
 */

/* Slab cache for fixed-size allocations */
struct slab_cache {
    const char *name;           /* Cache name for debugging */
    size_t obj_size;            /* Object size */
    size_t align;               /* Object alignment */
    size_t obj_per_slab;        /* Objects per slab */

    struct list_head slabs_full;    /* Full slabs */
    struct list_head slabs_partial; /* Partially full slabs */
    struct list_head slabs_free;    /* Empty slabs */

    u64 total_allocs;           /* Total allocations */
    u64 total_frees;            /* Total frees */
    u64 active_objs;            /* Currently allocated objects */
    u64 total_slabs;            /* Total slab count */

    spinlock_t lock;
    struct list_head cache_list; /* Link in global cache list */
};

/* Create a new slab cache */
struct slab_cache *slab_cache_create(const char *name, size_t size, size_t align);

/* Destroy a slab cache */
void slab_cache_destroy(struct slab_cache *cache);

/* Allocate from a slab cache */
void *slab_alloc(struct slab_cache *cache);

/* Free to a slab cache */
void slab_free(struct slab_cache *cache, void *obj);

/* Shrink a slab cache (free empty slabs) */
void slab_cache_shrink(struct slab_cache *cache);

/* Dump slab cache statistics */
void slab_cache_dump(struct slab_cache *cache);

/*
 * Kernel virtual memory allocation (vmalloc)
 */

/* Allocate virtually contiguous memory */
void *vmalloc(size_t size);

/* Free virtually contiguous memory */
void vfree(void *addr);

/* Map pages into kernel virtual space */
void *vmap(struct page **pages, u64 count, u64 flags);

/* Unmap pages from kernel virtual space */
void vunmap(void *addr);

#endif /* _OCEAN_VMM_H */
