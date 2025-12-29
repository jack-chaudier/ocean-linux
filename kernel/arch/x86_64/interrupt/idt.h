/*
 * Ocean Kernel - Interrupt Descriptor Table (IDT)
 *
 * x86_64 IDT setup for exception and interrupt handling.
 */

#ifndef _OCEAN_IDT_H
#define _OCEAN_IDT_H

#include <ocean/types.h>
#include <ocean/defs.h>

/*
 * IDT Entry (16 bytes in long mode)
 *
 * Format:
 *   Bytes 0-1:   Offset bits 0-15
 *   Bytes 2-3:   Segment selector
 *   Byte 4:      IST (bits 0-2), reserved (bits 3-7)
 *   Byte 5:      Type and attributes
 *   Bytes 6-7:   Offset bits 16-31
 *   Bytes 8-11:  Offset bits 32-63
 *   Bytes 12-15: Reserved (must be 0)
 */
struct idt_entry {
    u16 offset_low;         /* Offset bits 0-15 */
    u16 selector;           /* Code segment selector */
    u8  ist;                /* Interrupt Stack Table (0-7, 0 = none) */
    u8  type_attr;          /* Type and attributes */
    u16 offset_mid;         /* Offset bits 16-31 */
    u32 offset_high;        /* Offset bits 32-63 */
    u32 reserved;           /* Reserved, must be 0 */
} __packed;

/*
 * IDT Pointer (for LIDT instruction)
 */
struct idt_ptr {
    u16 limit;              /* Size of IDT - 1 */
    u64 base;               /* Linear address of IDT */
} __packed;

/*
 * Gate types
 */
#define IDT_TYPE_INTERRUPT  0x8E    /* Interrupt gate (clears IF) */
#define IDT_TYPE_TRAP       0x8F    /* Trap gate (IF unchanged) */
#define IDT_TYPE_INTERRUPT_USER  0xEE  /* Interrupt gate, DPL=3 */
#define IDT_TYPE_TRAP_USER      0xEF   /* Trap gate, DPL=3 */

/*
 * CPU Exception Vectors (0-31)
 */
#define VEC_DIVIDE_ERROR        0   /* #DE - Divide Error */
#define VEC_DEBUG               1   /* #DB - Debug */
#define VEC_NMI                 2   /* NMI - Non-Maskable Interrupt */
#define VEC_BREAKPOINT          3   /* #BP - Breakpoint */
#define VEC_OVERFLOW            4   /* #OF - Overflow */
#define VEC_BOUND_RANGE         5   /* #BR - Bound Range Exceeded */
#define VEC_INVALID_OPCODE      6   /* #UD - Invalid Opcode */
#define VEC_DEVICE_NOT_AVAIL    7   /* #NM - Device Not Available */
#define VEC_DOUBLE_FAULT        8   /* #DF - Double Fault */
#define VEC_COPROC_SEG          9   /* Coprocessor Segment Overrun (legacy) */
#define VEC_INVALID_TSS         10  /* #TS - Invalid TSS */
#define VEC_SEGMENT_NOT_PRESENT 11  /* #NP - Segment Not Present */
#define VEC_STACK_FAULT         12  /* #SS - Stack-Segment Fault */
#define VEC_GENERAL_PROTECTION  13  /* #GP - General Protection */
#define VEC_PAGE_FAULT          14  /* #PF - Page Fault */
#define VEC_RESERVED_15         15  /* Reserved */
#define VEC_X87_FP              16  /* #MF - x87 FP Exception */
#define VEC_ALIGNMENT_CHECK     17  /* #AC - Alignment Check */
#define VEC_MACHINE_CHECK       18  /* #MC - Machine Check */
#define VEC_SIMD_FP             19  /* #XM/#XF - SIMD FP Exception */
#define VEC_VIRTUALIZATION      20  /* #VE - Virtualization Exception */
#define VEC_CONTROL_PROTECTION  21  /* #CP - Control Protection Exception */
/* 22-27 Reserved */
#define VEC_HYPERVISOR          28  /* Hypervisor Injection */
#define VEC_VMM_COMM            29  /* VMM Communication */
#define VEC_SECURITY            30  /* #SX - Security Exception */
/* 31 Reserved */

/*
 * Hardware IRQ Vectors (remapped from 8259 PIC or APIC)
 * We remap IRQs to vectors 32-47 to avoid conflicts with exceptions
 */
#define VEC_IRQ_BASE            32
#define VEC_IRQ(n)              (VEC_IRQ_BASE + (n))

#define VEC_PIT                 VEC_IRQ(0)   /* Programmable Interval Timer */
#define VEC_KEYBOARD            VEC_IRQ(1)   /* Keyboard */
#define VEC_CASCADE             VEC_IRQ(2)   /* Cascade (for PIC) */
#define VEC_COM2                VEC_IRQ(3)   /* COM2 */
#define VEC_COM1                VEC_IRQ(4)   /* COM1 */
#define VEC_LPT2                VEC_IRQ(5)   /* LPT2 */
#define VEC_FLOPPY              VEC_IRQ(6)   /* Floppy */
#define VEC_LPT1                VEC_IRQ(7)   /* LPT1 / Spurious */
#define VEC_RTC                 VEC_IRQ(8)   /* Real-Time Clock */
#define VEC_IRQ9                VEC_IRQ(9)   /* ACPI */
#define VEC_IRQ10               VEC_IRQ(10)  /* Available */
#define VEC_IRQ11               VEC_IRQ(11)  /* Available */
#define VEC_PS2_MOUSE           VEC_IRQ(12)  /* PS/2 Mouse */
#define VEC_FPU                 VEC_IRQ(13)  /* FPU */
#define VEC_ATA_PRIMARY         VEC_IRQ(14)  /* Primary ATA */
#define VEC_ATA_SECONDARY       VEC_IRQ(15)  /* Secondary ATA */

/*
 * Software/APIC Vectors
 */
#define VEC_SYSCALL             0x80    /* System call (int 0x80) */
#define VEC_APIC_TIMER          0xFE    /* Local APIC Timer */
#define VEC_APIC_SPURIOUS       0xFF    /* APIC Spurious Interrupt */

/* IPI Vectors (Inter-Processor Interrupts) */
#define VEC_IPI_RESCHEDULE      0xF0    /* Reschedule IPI */
#define VEC_IPI_TLB_SHOOTDOWN   0xF1    /* TLB Shootdown */
#define VEC_IPI_CALL            0xF2    /* Function Call */
#define VEC_IPI_HALT            0xF3    /* Halt CPU */

/* Total number of IDT entries */
#define IDT_ENTRIES             256

/*
 * Trap Frame - saved by interrupt/exception entry
 *
 * This structure represents the CPU state saved on the stack
 * when an interrupt or exception occurs.
 */
struct trap_frame {
    /* Pushed by our interrupt stub */
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Interrupt number and error code */
    u64 int_no;
    u64 error_code;

    /* Pushed by CPU */
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;                /* Only present on privilege change */
    u64 ss;                 /* Only present on privilege change */
} __packed;

/*
 * Exception names (for debugging)
 */
extern const char *exception_names[32];

/* Interrupt handler function type */
typedef void (*isr_fn_t)(void);

/*
 * IDT Functions
 */
void idt_init(void);
void idt_set_gate(int vector, isr_fn_t handler, u8 type, u8 ist);
void idt_load(void);

/* Exception handler (C entry point) */
void exception_handler(struct trap_frame *frame);

/* IRQ handler (C entry point) */
void irq_handler(struct trap_frame *frame);

/* Register an IRQ handler callback */
typedef void (*irq_handler_t)(struct trap_frame *frame);
void irq_register(int irq, irq_handler_t handler);
void irq_unregister(int irq);

/* 8259 PIC functions */
void pic_remap(void);
void pic_disable(void);
void pic_unmask_irq(int irq);
void pic_mask_irq(int irq);

/*
 * Assembly interrupt stubs (defined in isr.asm)
 */
extern void isr0(void);     /* #DE Divide Error */
extern void isr1(void);     /* #DB Debug */
extern void isr2(void);     /* NMI */
extern void isr3(void);     /* #BP Breakpoint */
extern void isr4(void);     /* #OF Overflow */
extern void isr5(void);     /* #BR Bound Range */
extern void isr6(void);     /* #UD Invalid Opcode */
extern void isr7(void);     /* #NM Device Not Available */
extern void isr8(void);     /* #DF Double Fault */
extern void isr9(void);     /* Coprocessor Segment */
extern void isr10(void);    /* #TS Invalid TSS */
extern void isr11(void);    /* #NP Segment Not Present */
extern void isr12(void);    /* #SS Stack Fault */
extern void isr13(void);    /* #GP General Protection */
extern void isr14(void);    /* #PF Page Fault */
extern void isr15(void);    /* Reserved */
extern void isr16(void);    /* #MF x87 FP */
extern void isr17(void);    /* #AC Alignment Check */
extern void isr18(void);    /* #MC Machine Check */
extern void isr19(void);    /* #XM SIMD FP */
extern void isr20(void);    /* #VE Virtualization */
extern void isr21(void);    /* #CP Control Protection */
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* APIC/IPI stubs */
extern void isr_apic_timer(void);
extern void isr_apic_spurious(void);
extern void isr_ipi_reschedule(void);
extern void isr_ipi_tlb(void);
extern void isr_syscall(void);

#endif /* _OCEAN_IDT_H */
