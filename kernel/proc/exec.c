/*
 * Ocean Kernel - Program Execution
 *
 * Loads and executes ELF binaries.
 */

#include <ocean/process.h>
#include <ocean/sched.h>
#include <ocean/vmm.h>
#include <ocean/elf.h>
#include <ocean/boot.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern size_t strlen(const char *s);

/* From pmm.c */
extern void *get_free_page(unsigned int gfp_flags);
#define GFP_KERNEL 0
#define GFP_USER   1

/* From syscall entry */
extern void enter_usermode(u64 entry, u64 stack, u64 flags);
extern void enter_usermode_from_syscall(u64 entry, u64 stack, u64 flags);

/* From process.c */
extern struct thread *process_create_main_thread(struct process *proc, u64 entry, u64 stack_top);
extern struct process *init_process;

/* From vmm.c */
extern struct vm_area *vma_alloc(void);
extern void vma_insert(struct address_space *as, struct vm_area *vma);
extern void INIT_LIST_HEAD(struct list_head *list);
extern void vmm_destroy_address_space(struct address_space *as);
extern void paging_switch(struct address_space *as);

/* From sched */
extern struct thread *current_thread;

/*
 * User stack parameters
 */
#define USER_STACK_TOP      0x00007FFFFFFFE000ULL  /* Top of user stack */
#define USER_STACK_SIZE     (16 * PAGE_SIZE)        /* 64KB initial stack */

/*
 * Load an ELF segment into the address space
 */
static int segment_overlaps(struct address_space *as, u64 start, u64 end)
{
    struct vm_area *vma;

    list_for_each_entry(vma, &as->vma_list, list) {
        if (vma->start < end && vma->end > start) {
            return 1;
        }
        if (vma->start >= end) {
            break;
        }
    }

    return 0;
}

static void *user_ptr_to_kernel(struct address_space *as, u64 user_addr)
{
    const struct boot_info *boot = get_boot_info();
    phys_addr_t phys = paging_get_phys(as->pml4, user_addr);

    if (phys == (phys_addr_t)-1) {
        return NULL;
    }

    return (void *)(phys + boot->hhdm_offset);
}

static int write_user_bytes(struct address_space *as, u64 user_addr,
                            const void *src, size_t len)
{
    const u8 *bytes = (const u8 *)src;
    size_t copied = 0;

    while (copied < len) {
        u64 addr = user_addr + copied;
        size_t page_offset = (size_t)(addr & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - page_offset;
        void *dst;

        if (chunk > len - copied) {
            chunk = len - copied;
        }

        dst = user_ptr_to_kernel(as, addr);
        if (!dst) {
            return -1;
        }

        memcpy(dst, bytes + copied, chunk);
        copied += chunk;
    }

    return 0;
}

static int push_u64(struct address_space *as, u64 *sp, u64 value)
{
    *sp -= sizeof(value);
    return write_user_bytes(as, *sp, &value, sizeof(value));
}

static int load_segment(struct address_space *as, const void *elf_data, size_t elf_size,
                        const Elf64_Phdr *phdr)
{
    /* Only load PT_LOAD segments */
    if (phdr->p_type != PT_LOAD) {
        return 0;
    }

    u64 vaddr = phdr->p_vaddr;
    u64 filesz = phdr->p_filesz;
    u64 memsz = phdr->p_memsz;
    u64 offset = phdr->p_offset;

    /* Align to page boundaries */
    u64 vaddr_aligned = vaddr & ~(PAGE_SIZE - 1);
    u64 offset_in_page = vaddr - vaddr_aligned;
    u64 memsz_aligned = (memsz + offset_in_page + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (memsz < filesz) {
        kprintf("    Invalid ELF segment: filesz > memsz\n");
        return -1;
    }
    if (offset > elf_size || filesz > elf_size - offset) {
        kprintf("    Invalid ELF segment: file range out of bounds\n");
        return -1;
    }
    if (vaddr_aligned >= USER_SPACE_END ||
        memsz_aligned > USER_SPACE_END - vaddr_aligned) {
        kprintf("    Invalid ELF segment: virtual address out of range\n");
        return -1;
    }
    if (segment_overlaps(as, vaddr_aligned, vaddr_aligned + memsz_aligned)) {
        kprintf("    Invalid ELF segment: overlapping PT_LOAD range\n");
        return -1;
    }

    /* Determine page flags */
    u64 flags = PTE_PRESENT | PTE_USER;
    if (phdr->p_flags & PF_W) {
        flags |= PTE_WRITABLE;
    }
    if (!(phdr->p_flags & PF_X)) {
        flags |= PTE_NX;
    }

    /* Allocate and map pages */
    const struct boot_info *boot = get_boot_info();
    u64 hhdm = boot->hhdm_offset;

    for (u64 page_offset = 0; page_offset < memsz_aligned; page_offset += PAGE_SIZE) {
        u64 target_vaddr = vaddr_aligned + page_offset;

        /* Allocate a physical page */
        void *phys_page = get_free_page(GFP_USER);
        if (!phys_page) {
            kprintf("    Failed to allocate page!\n");
            return -1;
        }

        u64 phys_addr = (u64)phys_page - hhdm;

        /* Clear the page first */
        memset(phys_page, 0, PAGE_SIZE);

        /* Copy file data if this page overlaps with file content */
        u64 page_start = page_offset;
        if (page_start < offset_in_page + filesz) {
            u64 copy_start = 0;
            u64 copy_end = PAGE_SIZE;

            /* Adjust for alignment offset on first page */
            if (page_offset == 0) {
                copy_start = offset_in_page;
            }

            /* Adjust for file size limit */
            if (page_start + copy_end > offset_in_page + filesz) {
                copy_end = offset_in_page + filesz - page_start;
            }

            if (copy_end > copy_start) {
                u64 file_offset = offset + (page_offset - offset_in_page) +
                                  (page_offset == 0 ? 0 : copy_start);
                u64 copy_len = copy_end - copy_start;

                /* Ensure we don't read past file data */
                if (file_offset < offset + filesz) {
                    if (file_offset + copy_len > offset + filesz) {
                        copy_len = offset + filesz - file_offset;
                    }

                    memcpy((u8 *)phys_page + copy_start,
                           (const u8 *)elf_data + file_offset,
                           copy_len);
                }
            }
        }

        /* Map the page in user address space */
        paging_map(as->pml4, target_vaddr, phys_addr, flags);
    }

    /* Create a VMA for this segment */
    struct vm_area *vma = vma_alloc();
    if (!vma) {
        kprintf("    Failed to allocate VMA!\n");
        return -1;
    }

    /* Convert ELF segment flags to VMA flags */
    u32 vma_flags = VMA_READ;  /* Userspace access comes from page_prot */
    if (phdr->p_flags & PF_W) {
        vma_flags |= VMA_WRITE;
    }
    if (phdr->p_flags & PF_X) {
        vma_flags |= VMA_EXEC;
    }

    vma->start = vaddr_aligned;
    vma->end = vaddr_aligned + memsz_aligned;
    vma->flags = vma_flags;
    vma->page_prot = flags;  /* PTE flags */
    INIT_LIST_HEAD(&vma->list);

    vma_insert(as, vma);

    return 0;
}

/*
 * Set up user stack
 */
static u64 setup_user_stack(struct address_space *as, const char *const argv[])
{
    const struct boot_info *boot = get_boot_info();
    u64 hhdm = boot->hhdm_offset;
    u64 arg_addrs[32];
    int argc = 0;

    /* Allocate stack pages */
    u64 stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    for (u64 addr = stack_bottom; addr < USER_STACK_TOP; addr += PAGE_SIZE) {
        void *phys_page = get_free_page(GFP_USER);
        if (!phys_page) {
            kprintf("    Failed to allocate stack page!\n");
            return 0;
        }

        u64 phys_addr = (u64)phys_page - hhdm;
        memset(phys_page, 0, PAGE_SIZE);

        u64 flags = PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_NX;
        paging_map(as->pml4, addr, phys_addr, flags);
    }

    u64 sp = USER_STACK_TOP;

    /* Create VMA for the stack */
    struct vm_area *stack_vma = vma_alloc();
    if (!stack_vma) {
        kprintf("    Failed to allocate stack VMA!\n");
        return 0;
    }

    stack_vma->start = stack_bottom;
    stack_vma->end = USER_STACK_TOP;
    stack_vma->flags = VMA_READ | VMA_WRITE | VMA_STACK | VMA_ANONYMOUS;
    stack_vma->page_prot = PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_NX;
    INIT_LIST_HEAD(&stack_vma->list);

    vma_insert(as, stack_vma);

    if (argv) {
        while (argc < (int)(sizeof(arg_addrs) / sizeof(arg_addrs[0])) &&
               argv[argc]) {
            size_t len = strlen(argv[argc]) + 1;

            sp -= len;
            if (write_user_bytes(as, sp, argv[argc], len) < 0) {
                return 0;
            }

            arg_addrs[argc] = sp;
            argc++;
        }
    }

    sp &= ~0x7ULL;

    if (push_u64(as, &sp, 0) < 0) {
        return 0;
    }

    for (int i = argc - 1; i >= 0; i--) {
        if (push_u64(as, &sp, arg_addrs[i]) < 0) {
            return 0;
        }
    }

    if (push_u64(as, &sp, (u64)argc) < 0) {
        return 0;
    }

    return sp;
}

/*
 * Execute an ELF binary from memory
 *
 * elf_data: pointer to ELF file in kernel memory
 * elf_size: size of ELF file
 * name: process name
 *
 * Returns PID of new process or -1 on error
 */
pid_t exec_elf(const void *elf_data, size_t elf_size, const char *name)
{
    struct process *proc;
    struct thread *main_thread;
    const char *default_argv[] = { name ? name : "init", NULL };

    /* Validate ELF header */
    if (elf_size < sizeof(Elf64_Ehdr)) {
        kprintf("exec_elf: File too small\n");
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;

    int err = elf_validate(ehdr);
    if (err != ELF_OK) {
        kprintf("exec_elf: Invalid ELF (error %d)\n", err);
        return -1;
    }

    /* Create new process */
    proc = process_create(name);
    if (!proc) {
        kprintf("exec_elf: Failed to create process\n");
        return -1;
    }

    /* Create address space */
    proc->mm = vmm_create_address_space();
    if (!proc->mm) {
        kprintf("exec_elf: Failed to create address space\n");
        process_destroy(proc);
        return -1;
    }

    /* Load program segments */
    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)((const u8 *)elf_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (load_segment(proc->mm, elf_data, elf_size, &phdrs[i]) < 0) {
                kprintf("exec_elf: Failed to load segment %d\n", i);
                goto fail;
            }
        }
    }

    /* Set up user stack */
    u64 user_sp = setup_user_stack(proc->mm, default_argv);
    if (user_sp == 0) {
        kprintf("exec_elf: Failed to set up stack\n");
        goto fail;
    }

    /* Create main thread */
    main_thread = process_create_main_thread(proc, ehdr->e_entry, user_sp);
    if (!main_thread) {
        kprintf("exec_elf: Failed to create main thread\n");
        goto fail;
    }

    /* Mark as user thread */
    main_thread->flags &= ~TF_KTHREAD;

    /* Add to scheduler */
    thread_start(main_thread);

    return proc->pid;

fail:
    process_destroy(proc);
    return -1;
}

/*
 * Execute first user process (init)
 *
 * Called during kernel initialization to start the first user process.
 * This is typically called with an ELF file from the initrd.
 */
pid_t exec_init(const void *elf_data, size_t elf_size)
{
    pid_t pid = exec_elf(elf_data, elf_size, "init");

    if (pid > 0) {
        init_process = process_find(pid);
    }

    return pid;
}

/*
 * Simple test: execute a minimal test program
 *
 * This creates a tiny "program" that just makes syscalls.
 * Used for testing before we have a real init process.
 */
void exec_test_user_mode(void)
{
    struct process *proc;
    void *code_page;

    kprintf("\n=== Testing User Mode Entry ===\n");

    /* Create a minimal test process */
    proc = process_create("test");
    if (!proc) {
        kprintf("Failed to create test process\n");
        return;
    }

    /* Create address space */
    proc->mm = vmm_create_address_space();
    if (!proc->mm) {
        kprintf("Failed to create address space\n");
        process_destroy(proc);
        return;
    }

    const struct boot_info *boot = get_boot_info();
    u64 hhdm = boot->hhdm_offset;

    /*
     * Create a simple test program in user memory
     *
     * The program will:
     *   1. Call sys_write to print a message
     *   2. Call sys_exit
     */
    u64 code_vaddr = 0x400000;  /* Standard ELF base address */

    /* Allocate a page for code */
    code_page = get_free_page(GFP_USER);
    if (!code_page) {
        kprintf("Failed to allocate code page\n");
        process_destroy(proc);
        return;
    }

    u64 code_phys = (u64)code_page - hhdm;
    memset(code_page, 0, PAGE_SIZE);

    /* Write test program */
    u8 *code = (u8 *)code_page;
    int i = 0;

    /* Message string at offset 0x100 */
    const char *msg = "Hello from user mode!\n";
    size_t msg_len = 22;
    memcpy(code + 0x100, msg, msg_len);

    /* Code at offset 0:
     *
     * ; Call sys_write(1, msg, len)
     * mov rax, 33        ; SYS_WRITE
     * mov rdi, 1         ; fd = stdout
     * lea rsi, [rip+msg] ; buf = message
     * mov rdx, 22        ; len
     * syscall
     *
     * ; Call sys_exit(0)
     * mov rax, 0         ; SYS_EXIT
     * xor rdi, rdi       ; code = 0
     * syscall
     *
     * ; Infinite loop (shouldn't reach here)
     * jmp $
     */

    /* mov rax, 33 (SYS_WRITE) */
    code[i++] = 0x48; code[i++] = 0xc7; code[i++] = 0xc0;
    code[i++] = 33; code[i++] = 0; code[i++] = 0; code[i++] = 0;

    /* mov rdi, 1 */
    code[i++] = 0x48; code[i++] = 0xc7; code[i++] = 0xc7;
    code[i++] = 1; code[i++] = 0; code[i++] = 0; code[i++] = 0;

    /* lea rsi, [rip + offset_to_msg] */
    /* The msg is at 0x100, current position is ~i, so offset = 0x100 - (i + 7) */
    code[i++] = 0x48; code[i++] = 0x8d; code[i++] = 0x35;
    u32 msg_offset = 0x100 - (i + 4);
    code[i++] = msg_offset & 0xFF;
    code[i++] = (msg_offset >> 8) & 0xFF;
    code[i++] = (msg_offset >> 16) & 0xFF;
    code[i++] = (msg_offset >> 24) & 0xFF;

    /* mov rdx, 22 */
    code[i++] = 0x48; code[i++] = 0xc7; code[i++] = 0xc2;
    code[i++] = (u8)msg_len; code[i++] = 0; code[i++] = 0; code[i++] = 0;

    /* syscall */
    code[i++] = 0x0f; code[i++] = 0x05;

    /* mov rax, 0 (SYS_EXIT) */
    code[i++] = 0x48; code[i++] = 0xc7; code[i++] = 0xc0;
    code[i++] = 0; code[i++] = 0; code[i++] = 0; code[i++] = 0;

    /* xor rdi, rdi */
    code[i++] = 0x48; code[i++] = 0x31; code[i++] = 0xff;

    /* syscall */
    code[i++] = 0x0f; code[i++] = 0x05;

    /* jmp $ (infinite loop) */
    code[i++] = 0xeb; code[i++] = 0xfe;

    kprintf("Test program: %d bytes at 0x%llx\n", i, code_vaddr);

    /* Map code page as executable */
    u64 code_flags = PTE_PRESENT | PTE_USER;  /* Readable + executable */
    paging_map(proc->mm->pml4, code_vaddr, code_phys, code_flags);

    /* Set up stack */
    {
        const char *default_argv[] = { "test", NULL };
        u64 user_sp = setup_user_stack(proc->mm, default_argv);
        if (user_sp == 0) {
            kprintf("Failed to set up stack\n");
            process_destroy(proc);
            return;
        }

        kprintf("Test process: PID=%d, entry=0x%llx, stack=0x%llx\n",
                proc->pid, code_vaddr, user_sp);

        /* Switch to the new address space */
        paging_switch(proc->mm);
        kprintf("Switched to user address space\n");

        /* Enter user mode!
         * This won't return - the process will call sys_exit */
        kprintf("Entering user mode...\n\n");

        enter_usermode(code_vaddr, user_sp, 0x202);
    }

    /* Should never reach here */
    kprintf("ERROR: Returned from user mode!\n");
}

/*
 * Replace current process with new executable
 *
 * This is the actual exec() implementation that replaces the calling
 * process's address space with a new program and enters user mode.
 * This function does not return on success.
 */
int exec_replace(const void *elf_data, size_t elf_size, const char *name,
                 const char *const argv[])
{
    struct address_space *new_mm;
    struct address_space *old_mm;

    /* Validate ELF header */
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;

    if (elf_size < sizeof(Elf64_Ehdr)) {
        kprintf("exec: File too small\n");
        return -1;
    }

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        kprintf("exec: Not an ELF file\n");
        return -1;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        kprintf("exec: Not a 64-bit ELF\n");
        return -1;
    }

    if (ehdr->e_type != ET_EXEC) {
        kprintf("exec: Not an executable\n");
        return -1;
    }

    if (ehdr->e_machine != EM_X86_64) {
        kprintf("exec: Not x86_64\n");
        return -1;
    }

    /* Get current process and thread */
    struct thread *t = current_thread;
    struct process *proc = t->process;

    if (!proc) {
        kprintf("exec: No current process\n");
        return -1;
    }

    kprintf("exec: %s (pid %d)\n", name, proc->pid);

    /* Create new address space */
    new_mm = vmm_create_address_space();
    if (!new_mm) {
        kprintf("exec: Failed to create address space\n");
        return -1;
    }

    /* Load program segments */
    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)((const u8 *)elf_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (load_segment(new_mm, elf_data, elf_size, &phdrs[i]) < 0) {
                kprintf("exec: Failed to load segment %d\n", i);
                vmm_destroy_address_space(new_mm);
                return -1;
            }
        }
    }

    /* Set up user stack */
    u64 user_sp = setup_user_stack(new_mm, argv);
    if (user_sp == 0) {
        kprintf("exec: Failed to set up stack\n");
        vmm_destroy_address_space(new_mm);
        return -1;
    }

    old_mm = proc->mm;
    proc->mm = new_mm;

    if (name && *name) {
        size_t len = 0;

        while (name[len] != '\0' && len < sizeof(proc->name) - 1) {
            proc->name[len] = name[len];
            len++;
        }
        proc->name[len] = '\0';
    }

    /* Switch to new address space and enter user mode */
    paging_switch(new_mm);
    if (old_mm) {
        vmm_destroy_address_space(old_mm);
    }
    enter_usermode_from_syscall(ehdr->e_entry, user_sp, 0x202);

    /* Should never reach here */
    return -1;
}
