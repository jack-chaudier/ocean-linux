/*
 * Ocean Kernel - Interrupt Descriptor Table Implementation
 *
 * Sets up the IDT with exception and IRQ handlers.
 */

#include <ocean/types.h>
#include <ocean/defs.h>
#include "idt.h"
#include "../cpu/gdt.h"

/* External functions */
extern void *memset(void *s, int c, size_t n);
extern int kprintf(const char *fmt, ...);

/* The IDT itself */
static struct idt_entry idt[IDT_ENTRIES] __aligned(16);

/* IDT pointer for LIDT instruction */
static struct idt_ptr idtr;

/* IRQ handler table */
static irq_handler_t irq_handlers[16];

/* Exception names for debugging */
const char *exception_names[32] = {
    [VEC_DIVIDE_ERROR]        = "Divide Error (#DE)",
    [VEC_DEBUG]               = "Debug (#DB)",
    [VEC_NMI]                 = "Non-Maskable Interrupt",
    [VEC_BREAKPOINT]          = "Breakpoint (#BP)",
    [VEC_OVERFLOW]            = "Overflow (#OF)",
    [VEC_BOUND_RANGE]         = "Bound Range Exceeded (#BR)",
    [VEC_INVALID_OPCODE]      = "Invalid Opcode (#UD)",
    [VEC_DEVICE_NOT_AVAIL]    = "Device Not Available (#NM)",
    [VEC_DOUBLE_FAULT]        = "Double Fault (#DF)",
    [VEC_COPROC_SEG]          = "Coprocessor Segment Overrun",
    [VEC_INVALID_TSS]         = "Invalid TSS (#TS)",
    [VEC_SEGMENT_NOT_PRESENT] = "Segment Not Present (#NP)",
    [VEC_STACK_FAULT]         = "Stack-Segment Fault (#SS)",
    [VEC_GENERAL_PROTECTION]  = "General Protection (#GP)",
    [VEC_PAGE_FAULT]          = "Page Fault (#PF)",
    [VEC_RESERVED_15]         = "Reserved",
    [VEC_X87_FP]              = "x87 FP Exception (#MF)",
    [VEC_ALIGNMENT_CHECK]     = "Alignment Check (#AC)",
    [VEC_MACHINE_CHECK]       = "Machine Check (#MC)",
    [VEC_SIMD_FP]             = "SIMD FP Exception (#XM)",
    [VEC_VIRTUALIZATION]      = "Virtualization Exception (#VE)",
    [VEC_CONTROL_PROTECTION]  = "Control Protection (#CP)",
    [22] = "Reserved",
    [23] = "Reserved",
    [24] = "Reserved",
    [25] = "Reserved",
    [26] = "Reserved",
    [27] = "Reserved",
    [VEC_HYPERVISOR]          = "Hypervisor Injection",
    [VEC_VMM_COMM]            = "VMM Communication",
    [VEC_SECURITY]            = "Security Exception (#SX)",
    [31] = "Reserved"
};

/* Interrupt handler function type */
typedef void (*isr_fn_t)(void);

/*
 * Set an IDT gate
 */
void idt_set_gate(int vector, isr_fn_t handler, u8 type, u8 ist)
{
    u64 addr = (u64)handler;

    idt[vector].offset_low  = addr & 0xFFFF;
    idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].selector    = KERNEL_CS;
    idt[vector].ist         = ist & 0x7;
    idt[vector].type_attr   = type;
    idt[vector].reserved    = 0;
}

/*
 * Load the IDT
 */
void idt_load(void)
{
    __asm__ __volatile__("lidt (%0)" : : "r"(&idtr) : "memory");
}

/*
 * Initialize the IDT
 */
void idt_init(void)
{
    kprintf("Initializing IDT...\n");

    /* Clear IDT and handler table */
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    /*
     * Set up exception handlers (vectors 0-31)
     * Use IST 1 for double fault to ensure we have a valid stack
     */
    idt_set_gate(VEC_DIVIDE_ERROR,        isr0,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_DEBUG,               isr1,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_NMI,                 isr2,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_BREAKPOINT,          isr3,  IDT_TYPE_TRAP_USER, 0);  /* Allow from ring 3 */
    idt_set_gate(VEC_OVERFLOW,            isr4,  IDT_TYPE_TRAP_USER, 0);
    idt_set_gate(VEC_BOUND_RANGE,         isr5,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_INVALID_OPCODE,      isr6,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_DEVICE_NOT_AVAIL,    isr7,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_DOUBLE_FAULT,        isr8,  IDT_TYPE_TRAP, 1);  /* IST 1 for separate stack */
    idt_set_gate(VEC_COPROC_SEG,          isr9,  IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_INVALID_TSS,         isr10, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_SEGMENT_NOT_PRESENT, isr11, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_STACK_FAULT,         isr12, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_GENERAL_PROTECTION,  isr13, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_PAGE_FAULT,          isr14, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_RESERVED_15,         isr15, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_X87_FP,              isr16, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_ALIGNMENT_CHECK,     isr17, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_MACHINE_CHECK,       isr18, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_SIMD_FP,             isr19, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_VIRTUALIZATION,      isr20, IDT_TYPE_TRAP, 0);
    idt_set_gate(VEC_CONTROL_PROTECTION,  isr21, IDT_TYPE_TRAP, 0);
    idt_set_gate(22, isr22, IDT_TYPE_TRAP, 0);
    idt_set_gate(23, isr23, IDT_TYPE_TRAP, 0);
    idt_set_gate(24, isr24, IDT_TYPE_TRAP, 0);
    idt_set_gate(25, isr25, IDT_TYPE_TRAP, 0);
    idt_set_gate(26, isr26, IDT_TYPE_TRAP, 0);
    idt_set_gate(27, isr27, IDT_TYPE_TRAP, 0);
    idt_set_gate(28, isr28, IDT_TYPE_TRAP, 0);
    idt_set_gate(29, isr29, IDT_TYPE_TRAP, 0);
    idt_set_gate(30, isr30, IDT_TYPE_TRAP, 0);
    idt_set_gate(31, isr31, IDT_TYPE_TRAP, 0);

    /*
     * Set up hardware IRQ handlers (vectors 32-47)
     * These are interrupt gates (clear IF automatically)
     */
    idt_set_gate(VEC_PIT,           irq0,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_KEYBOARD,      irq1,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_CASCADE,       irq2,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_COM2,          irq3,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_COM1,          irq4,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_LPT2,          irq5,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_FLOPPY,        irq6,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_LPT1,          irq7,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_RTC,           irq8,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_IRQ9,          irq9,  IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_IRQ10,         irq10, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_IRQ11,         irq11, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_PS2_MOUSE,     irq12, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_FPU,           irq13, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_ATA_PRIMARY,   irq14, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_ATA_SECONDARY, irq15, IDT_TYPE_INTERRUPT, 0);

    /*
     * Set up APIC and IPI handlers
     */
    idt_set_gate(VEC_APIC_TIMER,      isr_apic_timer,    IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_APIC_SPURIOUS,   isr_apic_spurious, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_IPI_RESCHEDULE,  isr_ipi_reschedule, IDT_TYPE_INTERRUPT, 0);
    idt_set_gate(VEC_IPI_TLB_SHOOTDOWN, isr_ipi_tlb,     IDT_TYPE_INTERRUPT, 0);

    /*
     * System call interrupt (int 0x80)
     * This is a trap gate with DPL=3 so user code can invoke it
     */
    idt_set_gate(VEC_SYSCALL, isr_syscall, IDT_TYPE_TRAP_USER, 0);

    /* Set up IDTR */
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)&idt;

    /* Load IDT */
    idt_load();

    kprintf("IDT loaded: %d entries at 0x%llx\n", IDT_ENTRIES, (u64)&idt);
}

/*
 * Exception handler (C entry point)
 *
 * Called from assembly ISR stubs for CPU exceptions (vectors 0-31)
 */
void exception_handler(struct trap_frame *frame)
{
    if (frame->int_no == VEC_PAGE_FAULT) {
        extern void page_fault_handler(u64 error_code);
        page_fault_handler(frame->error_code);
        return;
    }

    const char *name = "Unknown";

    if (frame->int_no < 32) {
        name = exception_names[frame->int_no];
    }

    kprintf("\n!!! EXCEPTION: %s (vector %llu)\n", name, frame->int_no);
    kprintf("Error code: 0x%llx\n", frame->error_code);
    kprintf("\n");
    kprintf("RAX=%016llx  RBX=%016llx  RCX=%016llx  RDX=%016llx\n",
            frame->rax, frame->rbx, frame->rcx, frame->rdx);
    kprintf("RSI=%016llx  RDI=%016llx  RBP=%016llx  RSP=%016llx\n",
            frame->rsi, frame->rdi, frame->rbp, frame->rsp);
    kprintf("R8 =%016llx  R9 =%016llx  R10=%016llx  R11=%016llx\n",
            frame->r8, frame->r9, frame->r10, frame->r11);
    kprintf("R12=%016llx  R13=%016llx  R14=%016llx  R15=%016llx\n",
            frame->r12, frame->r13, frame->r14, frame->r15);
    kprintf("\n");
    kprintf("RIP=%016llx  CS=%04llx  RFLAGS=%016llx\n",
            frame->rip, frame->cs, frame->rflags);

    /* Page fault specific info */
    if (frame->int_no == VEC_PAGE_FAULT) {
        u64 cr2 = read_cr2();
        kprintf("CR2 (fault address): 0x%016llx\n", cr2);
        kprintf("Fault type: %s %s %s\n",
                (frame->error_code & 0x1) ? "protection" : "not-present",
                (frame->error_code & 0x2) ? "write" : "read",
                (frame->error_code & 0x4) ? "user" : "kernel");
    }

    /* Halt the CPU */
    kprintf("\nSystem halted.\n");
    for (;;) {
        cli();
        __asm__ __volatile__("hlt");
    }
}

/*
 * IRQ handler (C entry point)
 *
 * Called from assembly ISR stubs for hardware IRQs (vectors 32-47)
 */
void irq_handler(struct trap_frame *frame)
{
    int irq = (int)(frame->int_no - VEC_IRQ_BASE);

    /* Call registered handler if any */
    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](frame);
    }

    /*
     * Send End-Of-Interrupt to PIC
     *
     * For now, use legacy 8259 PIC. Will switch to APIC later.
     * If IRQ came from slave PIC (IRQ 8-15), send EOI to both.
     */
    if (irq >= 8) {
        outb(0xA0, 0x20);  /* EOI to slave PIC */
    }
    outb(0x20, 0x20);      /* EOI to master PIC */
}

/*
 * Register an IRQ handler
 */
void irq_register(int irq, irq_handler_t handler)
{
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
        kprintf("IRQ %d handler registered\n", irq);
    }
}

/*
 * Unregister an IRQ handler
 */
void irq_unregister(int irq)
{
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

/*
 * Remap the 8259 PIC
 *
 * By default, the PIC uses vectors 0-15 which conflict with CPU exceptions.
 * We remap IRQs to vectors 32-47.
 */
void pic_remap(void)
{
    /* Save masks */
    u8 mask1 = inb(0x21);
    u8 mask2 = inb(0xA1);

    /* Start initialization sequence (ICW1) */
    outb(0x20, 0x11);  /* Master PIC */
    outb(0xA0, 0x11);  /* Slave PIC */
    io_wait();

    /* ICW2: Set vector offsets */
    outb(0x21, VEC_IRQ_BASE);      /* Master: vectors 32-39 */
    outb(0xA1, VEC_IRQ_BASE + 8);  /* Slave: vectors 40-47 */
    io_wait();

    /* ICW3: Tell Master that Slave is at IRQ2 */
    outb(0x21, 0x04);  /* Master: slave at IRQ2 (bit 2) */
    outb(0xA1, 0x02);  /* Slave: cascade identity 2 */
    io_wait();

    /* ICW4: Set 8086 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();

    /* Restore masks (all masked for now) */
    outb(0x21, mask1);
    outb(0xA1, mask2);

    kprintf("PIC remapped: IRQs at vectors %d-%d\n", VEC_IRQ_BASE, VEC_IRQ_BASE + 15);
}

/*
 * Disable the 8259 PIC (for APIC-only mode)
 */
void pic_disable(void)
{
    outb(0xA1, 0xFF);  /* Mask all IRQs on slave */
    outb(0x21, 0xFF);  /* Mask all IRQs on master */
    kprintf("8259 PIC disabled\n");
}

/*
 * Enable a specific IRQ on the PIC
 */
void pic_unmask_irq(int irq)
{
    u16 port;
    u8 mask;

    if (irq < 8) {
        port = 0x21;  /* Master PIC data port */
    } else {
        port = 0xA1;  /* Slave PIC data port */
        irq -= 8;
    }

    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}

/*
 * Disable a specific IRQ on the PIC
 */
void pic_mask_irq(int irq)
{
    u16 port;
    u8 mask;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }

    mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}
