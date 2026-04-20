/*
 * Ocean Kernel - Per-process IPC window
 *
 * Each user process owns one private 4 KiB page mapped at
 * OCEAN_IPC_WINDOW_VA. Protocols reference slices of this page as
 * (offset, length) pairs; the kernel copies slices across processes as part
 * of call/reply. Threads inside a single process share one window; fine as
 * long as userspace stays single-threaded, and a natural place to carve up
 * per-thread sub-regions later.
 */

#include <ocean/ipc.h>
#include <ocean/ipc_proto.h>
#include <ocean/process.h>
#include <ocean/vmm.h>
#include <ocean/boot.h>
#include <ocean/types.h>
#include <ocean/defs.h>

extern int kprintf(const char *fmt, ...);

/*
 * Allocate and map the IPC window into an address space.
 *
 * Returns 0 on success, -1 on failure. On success the window is available at
 * OCEAN_IPC_WINDOW_VA and proc->ipc_window_phys records the backing physical
 * address so the kernel can reach it via the HHDM mapping.
 *
 * Safe to call when an IPC window is already set up (it first tears down the
 * previous mapping).
 */
int process_setup_ipc_window(struct process *proc)
{
    if (!proc || !proc->mm) {
        return -1;
    }

    /* Tear down any prior window so exec can re-use this helper cleanly. */
    if (proc->ipc_window_phys) {
        vmm_unmap_region(proc->mm, OCEAN_IPC_WINDOW_VA, OCEAN_IPC_WINDOW_SIZE);
        proc->ipc_window_phys = 0;
    }

    /* VMA_READ | VMA_WRITE | VMA_ANONYMOUS maps to USER + WRITABLE + NX in
     * vma_to_pte_flags, which is what we want: userspace reads/writes the
     * page directly but never executes it. */
    u32 flags = VMA_READ | VMA_WRITE | VMA_ANONYMOUS;
    if (vmm_map_region(proc->mm, OCEAN_IPC_WINDOW_VA,
                       OCEAN_IPC_WINDOW_SIZE, flags) != 0) {
        return -1;
    }

    phys_addr_t phys = paging_get_phys(proc->mm->pml4, OCEAN_IPC_WINDOW_VA);
    if (!phys) {
        vmm_unmap_region(proc->mm, OCEAN_IPC_WINDOW_VA, OCEAN_IPC_WINDOW_SIZE);
        return -1;
    }

    proc->ipc_window_phys = (u64)phys;
    return 0;
}

/*
 * Record a process's IPC window phys after its address space was cloned from
 * a parent (fork). vmm_clone_address_space allocated a fresh page for the
 * cloned VMA; we just need to read back the new phys so call/reply can find
 * it. No-op if the parent had no window set up.
 */
void process_adopt_ipc_window(struct process *proc)
{
    if (!proc || !proc->mm) {
        return;
    }

    phys_addr_t phys = paging_get_phys(proc->mm->pml4, OCEAN_IPC_WINDOW_VA);
    proc->ipc_window_phys = (u64)phys;
}

/*
 * Kernel-accessible pointer into a process's IPC window, or NULL if the
 * process has no window mapped. Uses the HHDM mapping so the kernel can
 * memcpy directly without switching address spaces.
 */
void *process_ipc_window_kva(struct process *proc)
{
    if (!proc || !proc->ipc_window_phys) {
        return NULL;
    }

    const struct boot_info *boot = get_boot_info();
    return (void *)(proc->ipc_window_phys + boot->hhdm_offset);
}
