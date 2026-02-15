/*
 * Ocean Kernel - Page Fault Handler
 *
 * Handles page faults for demand paging, copy-on-write,
 * and stack growth.
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

/* Boot info */
extern const struct boot_info *get_boot_info(void);

/* Current address space (will be per-CPU later) */
static struct address_space *current_as = NULL;

/*
 * Set current address space
 */
void vmm_set_current(struct address_space *as)
{
    current_as = as;
}

/*
 * Get current address space
 */
struct address_space *vmm_get_current(void)
{
    return current_as ? current_as : &kernel_space;
}

/*
 * Handle a Copy-on-Write fault
 */
static int handle_cow_fault(struct address_space *as, u64 fault_addr, pte_t *pte)
{
    phys_addr_t old_phys = *pte & PTE_ADDR_MASK;

    /* Allocate a new page */
    void *new_page = get_free_page();
    if (!new_page) {
        return -1;
    }

    /* Copy contents from old page */
    const struct boot_info *boot = get_boot_info();
    void *old_page = (void *)(old_phys + boot->hhdm_offset);
    memcpy(new_page, old_page, PAGE_SIZE);

    /* Update PTE: new physical address, remove COW flag, add write permission */
    phys_addr_t new_phys = (phys_addr_t)new_page - boot->hhdm_offset;
    *pte = new_phys | ((*pte & ~(PTE_ADDR_MASK | PTE_COW)) | PTE_WRITABLE);

    tlb_flush_page(fault_addr & ~(PAGE_SIZE - 1));

    /* TODO: Decrement reference count on old page, free if zero */

    return 0;
}

/*
 * Handle demand paging for a VMA (allocate page on first access)
 */
static int handle_demand_fault(struct address_space *as, u64 fault_addr,
                               struct vm_area *vma)
{
    /* Allocate a new page */
    void *page = get_free_page();
    if (!page) {
        return -1;
    }

    /* Zero the page */
    memset(page, 0, PAGE_SIZE);

    /* Map it */
    const struct boot_info *boot = get_boot_info();
    phys_addr_t phys = (phys_addr_t)page - boot->hhdm_offset;
    u64 page_addr = fault_addr & ~(PAGE_SIZE - 1);

    if (paging_map(as->pml4, page_addr, phys, vma->page_prot) != 0) {
        free_page(page);
        return -1;
    }

    return 0;
}

/*
 * Handle stack growth
 */
static int handle_stack_growth(struct address_space *as, u64 fault_addr,
                               struct vm_area *vma)
{
    /* Check if fault is near the VMA (within a few pages) */
    u64 page_addr = fault_addr & ~(PAGE_SIZE - 1);

    /* Allow stack to grow down by up to 256 pages at a time */
    if (page_addr >= vma->start - (256 * PAGE_SIZE) && page_addr < vma->start) {
        /* Extend VMA downward */
        u64 new_start = page_addr & ~(PAGE_SIZE - 1);

        /* Allocate pages for the new region */
        for (u64 addr = new_start; addr < vma->start; addr += PAGE_SIZE) {
            void *page = get_free_page();
            if (!page) {
                return -1;
            }
            memset(page, 0, PAGE_SIZE);

            const struct boot_info *boot = get_boot_info();
            phys_addr_t phys = (phys_addr_t)page - boot->hhdm_offset;

            if (paging_map(as->pml4, addr, phys, vma->page_prot) != 0) {
                free_page(page);
                return -1;
            }
        }

        u64 added_pages = (vma->start - new_start) / PAGE_SIZE;
        vma->start = new_start;
        as->total_vm += added_pages;

        return 0;
    }

    return -1;
}

/*
 * Main page fault handler
 *
 * Called from the exception handler when a #PF occurs.
 *
 * Error code bits:
 *   0: Present (0=not present, 1=protection violation)
 *   1: Write (0=read, 1=write)
 *   2: User (0=kernel, 1=user)
 *   3: Reserved bit set
 *   4: Instruction fetch
 */
int vmm_page_fault(u64 fault_addr, u64 error_code)
{
    struct address_space *as = vmm_get_current();

    bool is_present = error_code & PF_PRESENT;
    bool is_write = error_code & PF_WRITE;
    bool is_user = error_code & PF_USER;

    /* Kernel faults are always fatal for now */
    if (!is_user && fault_addr >= KERNEL_SPACE_START) {
        kprintf("Kernel page fault at 0x%llx (error 0x%llx)\n",
                fault_addr, error_code);
        return -1;
    }

    /* Find VMA containing the fault address */
    struct vm_area *vma = vmm_find_vma(as, fault_addr);

    /* Check for stack growth (fault just below a stack VMA) */
    if (!vma) {
        /* Look for stack VMA that this might be trying to grow */
        struct vm_area *stack_vma;
        list_for_each_entry(stack_vma, &as->vma_list, list) {
            if ((stack_vma->flags & VMA_STACK) &&
                fault_addr < stack_vma->start &&
                fault_addr >= stack_vma->start - (256 * PAGE_SIZE)) {
                if (handle_stack_growth(as, fault_addr, stack_vma) == 0) {
                    return 0;
                }
                break;
            }
        }

        /* No VMA found - invalid access */
        kprintf("Page fault: no VMA for address 0x%llx\n", fault_addr);
        return -1;
    }

    /* Check permissions */
    if (is_write && !(vma->flags & VMA_WRITE)) {
        /* Write to read-only region */
        /* Check for COW */
        pte_t *pte = paging_get_pte(as->pml4, fault_addr);
        if (pte && (*pte & PTE_COW)) {
            return handle_cow_fault(as, fault_addr, pte);
        }
        kprintf("Page fault: write to read-only VMA at 0x%llx\n", fault_addr);
        return -1;
    }

    /* Check if page is present */
    if (!is_present) {
        /* Demand paging - allocate page on first access */
        return handle_demand_fault(as, fault_addr, vma);
    }

    /* Protection fault on present page - COW? */
    if (is_write) {
        pte_t *pte = paging_get_pte(as->pml4, fault_addr);
        if (pte && (*pte & PTE_COW)) {
            return handle_cow_fault(as, fault_addr, pte);
        }
    }

    /* Unknown fault reason */
    kprintf("Page fault: unhandled at 0x%llx (error 0x%llx)\n",
            fault_addr, error_code);
    return -1;
}

/*
 * Called from IDT exception handler for #PF
 */
void page_fault_handler(u64 error_code)
{
    u64 fault_addr = read_cr2();

    if (vmm_page_fault(fault_addr, error_code) != 0) {
        /* Fault could not be handled - this is a fatal error */
        extern void panic(const char *fmt, ...) __noreturn;
        panic("Unhandled page fault at 0x%lx (error 0x%lx)",
              fault_addr, error_code);
    }
}
