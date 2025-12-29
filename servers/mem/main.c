/*
 * Ocean Memory Server
 *
 * Userspace memory management server that handles:
 *   - Physical page allocation tracking
 *   - Virtual memory mapping requests
 *   - Memory grants between processes
 *
 * This server delegates actual memory operations to the kernel
 * via syscalls, but tracks ownership and enforces policy.
 */

#include <stdio.h>
#include <string.h>
#include <ocean/syscall.h>

/* Will use shared protocol definitions once available */
#include <ocean/ipc_proto.h>

#define MEM_VERSION "0.1.0"
#define MAX_ALLOCS  256

/* Allocation tracking */
struct mem_alloc {
    uint64_t phys_addr;     /* Physical address */
    uint64_t pages;         /* Number of pages */
    uint32_t owner_pid;     /* Owning process */
    uint32_t flags;         /* Allocation flags */
};

static struct mem_alloc allocs[MAX_ALLOCS];
static int num_allocs = 0;
static int mem_endpoint = -1;

/* Statistics */
static uint64_t total_pages_allocated = 0;
static uint64_t total_alloc_requests = 0;
static uint64_t total_free_requests = 0;

/*
 * Initialize the memory server
 */
static void mem_init(void)
{
    printf("[mem] Memory Server v%s starting\n", MEM_VERSION);

    /* Initialize allocation table */
    memset(allocs, 0, sizeof(allocs));

    /* Create our IPC endpoint */
    mem_endpoint = endpoint_create(0);
    if (mem_endpoint < 0) {
        printf("[mem] Failed to create endpoint: %d\n", mem_endpoint);
        return;
    }
    printf("[mem] Created endpoint %d\n", mem_endpoint);

    /* TODO: Register with init server as EP_MEM */

    printf("[mem] Memory server initialized\n");
}

/*
 * Find a free allocation slot
 */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (allocs[i].pages == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Find allocation by physical address
 */
static int find_alloc(uint64_t phys_addr)
{
    for (int i = 0; i < MAX_ALLOCS; i++) {
        if (allocs[i].phys_addr == phys_addr && allocs[i].pages > 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Handle MEM_ALLOC_PHYS request
 * Allocates physical pages and tracks ownership
 */
static int handle_alloc_phys(uint32_t client_pid, uint64_t pages, uint64_t flags,
                             uint64_t *out_addr)
{
    total_alloc_requests++;

    if (pages == 0 || pages > 1024) {
        printf("[mem] Invalid allocation size: %llu pages\n",
               (unsigned long long)pages);
        return E_INVAL;
    }

    int slot = find_free_slot();
    if (slot < 0) {
        printf("[mem] No free allocation slots\n");
        return E_NOMEM;
    }

    /* TODO: Actually allocate physical pages via kernel syscall
     * For now, just simulate with a fake address
     */
    uint64_t phys_addr = 0x100000 + (num_allocs * 0x1000 * pages);

    /* Record the allocation */
    allocs[slot].phys_addr = phys_addr;
    allocs[slot].pages = pages;
    allocs[slot].owner_pid = client_pid;
    allocs[slot].flags = (uint32_t)flags;
    num_allocs++;

    total_pages_allocated += pages;
    *out_addr = phys_addr;

    printf("[mem] Allocated %llu pages at 0x%llx for PID %u\n",
           (unsigned long long)pages,
           (unsigned long long)phys_addr,
           client_pid);

    return E_OK;
}

/*
 * Handle MEM_FREE_PHYS request
 */
static int handle_free_phys(uint32_t client_pid, uint64_t phys_addr)
{
    total_free_requests++;

    int slot = find_alloc(phys_addr);
    if (slot < 0) {
        printf("[mem] Unknown allocation at 0x%llx\n",
               (unsigned long long)phys_addr);
        return E_NOENT;
    }

    /* Check ownership */
    if (allocs[slot].owner_pid != client_pid) {
        printf("[mem] PID %u cannot free memory owned by PID %u\n",
               client_pid, allocs[slot].owner_pid);
        return E_PERM;
    }

    /* TODO: Actually free physical pages via kernel syscall */

    uint64_t freed_pages = allocs[slot].pages;
    total_pages_allocated -= freed_pages;

    allocs[slot].phys_addr = 0;
    allocs[slot].pages = 0;
    allocs[slot].owner_pid = 0;
    allocs[slot].flags = 0;
    num_allocs--;

    printf("[mem] Freed %llu pages at 0x%llx\n",
           (unsigned long long)freed_pages,
           (unsigned long long)phys_addr);

    return E_OK;
}

/*
 * Handle MEM_MAP request
 */
static int handle_map(uint32_t client_pid, uint64_t virt_addr, uint64_t phys_addr,
                      uint64_t pages, uint64_t flags, uint64_t *out_virt)
{
    (void)flags;

    /* TODO: Implement virtual memory mapping via kernel syscall */
    printf("[mem] Map request: PID %u, virt=0x%llx, phys=0x%llx, pages=%llu\n",
           client_pid,
           (unsigned long long)virt_addr,
           (unsigned long long)phys_addr,
           (unsigned long long)pages);

    /* For now, just return the requested address */
    *out_virt = virt_addr ? virt_addr : 0x40000000;

    return E_OK;
}

/*
 * Handle MEM_QUERY request
 */
static int handle_query(uint64_t *out_total, uint64_t *out_allocs,
                        uint64_t *out_frees)
{
    *out_total = total_pages_allocated;
    *out_allocs = total_alloc_requests;
    *out_frees = total_free_requests;
    return E_OK;
}

/*
 * Process incoming IPC messages
 */
static void mem_serve(void)
{
    printf("[mem] Entering service loop\n");

    /* For now, just yield since we don't have full IPC recv implemented */
    for (int i = 0; i < 50; i++) {
        yield();

        /* Simulate handling a request every 10 ticks */
        if (i % 10 == 0 && i > 0) {
            uint64_t addr = 0;
            int err = handle_alloc_phys(1, 4, 0, &addr);
            if (err == E_OK) {
                printf("[mem] Self-test allocation at 0x%llx\n",
                       (unsigned long long)addr);
            }
        }
    }
}

/*
 * Print memory statistics
 */
static void mem_stats(void)
{
    printf("\n[mem] Memory Server Statistics:\n");
    printf("  Total pages allocated: %llu\n",
           (unsigned long long)total_pages_allocated);
    printf("  Allocation requests: %llu\n",
           (unsigned long long)total_alloc_requests);
    printf("  Free requests: %llu\n",
           (unsigned long long)total_free_requests);
    printf("  Active allocations: %d\n", num_allocs);
    printf("\n");
}

/*
 * Main entry point
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n========================================\n");
    printf("  Ocean Memory Server v%s\n", MEM_VERSION);
    printf("========================================\n\n");

    printf("[mem] PID: %d, PPID: %d\n", getpid(), getppid());

    mem_init();
    mem_serve();
    mem_stats();

    printf("[mem] Memory server exiting\n");
    return 0;
}
