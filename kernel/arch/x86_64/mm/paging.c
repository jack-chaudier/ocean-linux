/*
 * Ocean Kernel - x86_64 Paging
 *
 * 4-level page table management for x86_64.
 * PML4 -> PDPT -> PD -> PT -> Page
 */

#include <ocean/vmm.h>
#include <ocean/pmm.h>
#include <ocean/boot.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/* PMM functions */
extern void *simple_get_free_page(void);
extern void simple_free_page(void *addr);
#define get_free_page() simple_get_free_page()
#define free_page(addr) simple_free_page(addr)

/* Boot info for HHDM offset */
extern const struct boot_info *get_boot_info(void);

/* Kernel's PML4 (set up by bootloader, we'll copy kernel mappings from it) */
static pml4e_t *kernel_pml4;
static phys_addr_t kernel_pml4_phys;

/*
 * Convert physical address to virtual using HHDM
 */
static inline void *phys_to_virt_local(phys_addr_t phys)
{
    const struct boot_info *boot = get_boot_info();
    return (void *)(phys + boot->hhdm_offset);
}

/*
 * Convert virtual address to physical (for HHDM addresses)
 */
static inline phys_addr_t virt_to_phys_local(void *virt)
{
    const struct boot_info *boot = get_boot_info();
    return (phys_addr_t)virt - boot->hhdm_offset;
}

/*
 * Allocate a page table page (zeroed)
 */
static void *alloc_pt_page(void)
{
    void *page = get_free_page();
    if (page) {
        memset(page, 0, PAGE_SIZE);
    }
    return page;
}

/*
 * Free a page table page
 */
static void free_pt_page(void *page)
{
    free_page(page);
}

/*
 * Get or create a page table entry at a given level
 * Returns pointer to the entry, creating intermediate tables as needed
 */
static u64 *get_or_create_pte(pml4e_t *pml4, u64 virt, bool create)
{
    u64 pml4_idx = PML4_INDEX(virt);
    u64 pdpt_idx = PDPT_INDEX(virt);
    u64 pd_idx = PD_INDEX(virt);
    u64 pt_idx = PT_INDEX(virt);

    /* Get PDPT */
    pml4e_t *pml4_entry = &pml4[pml4_idx];
    pdpe_t *pdpt;

    if (!(*pml4_entry & PTE_PRESENT)) {
        if (!create) return NULL;

        void *new_pdpt = alloc_pt_page();
        if (!new_pdpt) return NULL;

        phys_addr_t pdpt_phys = virt_to_phys_local(new_pdpt);
        *pml4_entry = pdpt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pdpt = (pdpe_t *)new_pdpt;
    } else {
        pdpt = (pdpe_t *)phys_to_virt_local(*pml4_entry & PTE_ADDR_MASK);
    }

    /* Get PD */
    pdpe_t *pdpt_entry = &pdpt[pdpt_idx];
    pde_t *pd;

    if (!(*pdpt_entry & PTE_PRESENT)) {
        if (!create) return NULL;

        void *new_pd = alloc_pt_page();
        if (!new_pd) return NULL;

        phys_addr_t pd_phys = virt_to_phys_local(new_pd);
        *pdpt_entry = pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pd = (pde_t *)new_pd;
    } else {
        /* Check for 1GB huge page */
        if (*pdpt_entry & PTE_HUGE) {
            return NULL; /* Can't get PT entry in huge page */
        }
        pd = (pde_t *)phys_to_virt_local(*pdpt_entry & PTE_ADDR_MASK);
    }

    /* Get PT */
    pde_t *pd_entry = &pd[pd_idx];
    pte_t *pt;

    if (!(*pd_entry & PTE_PRESENT)) {
        if (!create) return NULL;

        void *new_pt = alloc_pt_page();
        if (!new_pt) return NULL;

        phys_addr_t pt_phys = virt_to_phys_local(new_pt);
        *pd_entry = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        pt = (pte_t *)new_pt;
    } else {
        /* Check for 2MB huge page */
        if (*pd_entry & PTE_HUGE) {
            return NULL; /* Can't get PT entry in huge page */
        }
        pt = (pte_t *)phys_to_virt_local(*pd_entry & PTE_ADDR_MASK);
    }

    return &pt[pt_idx];
}

/*
 * Get PTE for an address (without creating)
 */
pte_t *paging_get_pte(pml4e_t *pml4, u64 virt)
{
    return (pte_t *)get_or_create_pte(pml4, virt, false);
}

/*
 * Map a single page
 */
int paging_map(pml4e_t *pml4, u64 virt, phys_addr_t phys, u64 flags)
{
    pte_t *pte = get_or_create_pte(pml4, virt, true);
    if (!pte) {
        return -1;
    }

    /* Check if already mapped */
    if (*pte & PTE_PRESENT) {
        /* Already mapped - could be an error or intentional remap */
        tlb_flush_page(virt);
    }

    *pte = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    tlb_flush_page(virt);

    return 0;
}

/*
 * Unmap a single page
 */
int paging_unmap(pml4e_t *pml4, u64 virt)
{
    pte_t *pte = paging_get_pte(pml4, virt);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return -1; /* Not mapped */
    }

    *pte = 0;
    tlb_flush_page(virt);

    return 0;
}

/*
 * Map a range of pages
 */
int paging_map_range(pml4e_t *pml4, u64 virt, phys_addr_t phys,
                     u64 size, u64 flags)
{
    u64 end = virt + size;

    for (u64 addr = virt; addr < end; addr += PAGE_SIZE, phys += PAGE_SIZE) {
        if (paging_map(pml4, addr, phys, flags) != 0) {
            /* Rollback on failure */
            for (u64 rb = virt; rb < addr; rb += PAGE_SIZE) {
                paging_unmap(pml4, rb);
            }
            return -1;
        }
    }

    return 0;
}

/*
 * Unmap a range of pages
 */
int paging_unmap_range(pml4e_t *pml4, u64 virt, u64 size)
{
    u64 end = virt + size;

    for (u64 addr = virt; addr < end; addr += PAGE_SIZE) {
        paging_unmap(pml4, addr);
    }

    return 0;
}

/*
 * Get physical address for a virtual address
 */
phys_addr_t paging_get_phys(pml4e_t *pml4, u64 virt)
{
    pte_t *pte = paging_get_pte(pml4, virt);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return (phys_addr_t)-1;
    }

    return (*pte & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

/*
 * Create a new PML4 for a user process
 * Copies kernel mappings (upper half) from kernel PML4
 */
pml4e_t *paging_create_pml4(void)
{
    void *new_pml4 = alloc_pt_page();
    if (!new_pml4) {
        return NULL;
    }

    pml4e_t *pml4 = (pml4e_t *)new_pml4;

    /* Copy kernel mappings (entries 256-511, the upper half) */
    if (kernel_pml4) {
        for (int i = 256; i < 512; i++) {
            pml4[i] = kernel_pml4[i];
        }
    }

    return pml4;
}

/*
 * Free page tables recursively
 */
static void free_pt_recursive(u64 *table, int level)
{
    for (int i = 0; i < PT_ENTRIES; i++) {
        if (table[i] & PTE_PRESENT) {
            if (level > 0 && !(table[i] & PTE_HUGE)) {
                /* Recurse into lower level */
                u64 *lower = (u64 *)phys_to_virt_local(table[i] & PTE_ADDR_MASK);
                free_pt_recursive(lower, level - 1);
            }
            /* Level 0 (PT) entries point to actual pages, don't free those here */
        }
    }
    free_pt_page(table);
}

/*
 * Destroy a PML4 and all associated user-space page tables
 */
void paging_destroy_pml4(pml4e_t *pml4)
{
    if (!pml4) return;

    /* Only free user-space page tables (entries 0-255) */
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PTE_PRESENT) {
            pdpe_t *pdpt = (pdpe_t *)phys_to_virt_local(pml4[i] & PTE_ADDR_MASK);

            for (int j = 0; j < PT_ENTRIES; j++) {
                if (pdpt[j] & PTE_PRESENT && !(pdpt[j] & PTE_HUGE)) {
                    pde_t *pd = (pde_t *)phys_to_virt_local(pdpt[j] & PTE_ADDR_MASK);

                    for (int k = 0; k < PT_ENTRIES; k++) {
                        if (pd[k] & PTE_PRESENT && !(pd[k] & PTE_HUGE)) {
                            pte_t *pt = (pte_t *)phys_to_virt_local(pd[k] & PTE_ADDR_MASK);
                            free_pt_page(pt);
                        }
                    }
                    free_pt_page(pd);
                }
            }
            free_pt_page(pdpt);
        }
    }

    free_pt_page(pml4);
}

/*
 * Switch to a different address space
 */
void paging_switch(struct address_space *as)
{
    if (as && as->pml4_phys) {
        write_cr3(as->pml4_phys);
        vmm_set_current(as);
    }
}

/*
 * Initialize paging subsystem
 *
 * Called after PMM is initialized. The bootloader (Limine) has already
 * set up page tables for us with HHDM mapping. We save the kernel's
 * PML4 so we can copy kernel mappings to new address spaces.
 */
void paging_init(void)
{
    kprintf("Initializing paging...\n");

    /* Get current PML4 from CR3 */
    kernel_pml4_phys = read_cr3() & PTE_ADDR_MASK;
    kernel_pml4 = (pml4e_t *)phys_to_virt_local(kernel_pml4_phys);

    kprintf("  Kernel PML4 at phys 0x%llx, virt 0x%p\n",
            kernel_pml4_phys, kernel_pml4);

    /* Count mapped kernel entries */
    int kernel_entries = 0;
    for (int i = 256; i < 512; i++) {
        if (kernel_pml4[i] & PTE_PRESENT) {
            kernel_entries++;
        }
    }
    kprintf("  Kernel PML4 entries (256-511): %d\n", kernel_entries);

    kprintf("Paging initialized\n");
}

/*
 * Kernel address space structure
 */
struct address_space kernel_space;

/*
 * Initialize the kernel address space
 */
void kernel_space_init(void)
{
    kernel_space.pml4 = kernel_pml4;
    kernel_space.pml4_phys = kernel_pml4_phys;
    INIT_LIST_HEAD(&kernel_space.vma_list);
    kernel_space.vma_count = 0;
    kernel_space.ref_count = 1;
    spin_init(&kernel_space.lock);
}
