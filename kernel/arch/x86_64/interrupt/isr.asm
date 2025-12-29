;
; Ocean Kernel - Interrupt Service Routine Stubs
;
; Assembly entry points for all CPU exceptions and hardware IRQs.
; These save the CPU state, call the C handler, and restore state.
;

bits 64
section .text

; External C handlers
extern exception_handler
extern irq_handler

; Export all ISR stubs
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
global isr_apic_timer, isr_apic_spurious
global isr_ipi_reschedule, isr_ipi_tlb
global isr_syscall

;------------------------------------------------------------------------------
; Macro: ISR without error code
; CPU pushes: SS, RSP, RFLAGS, CS, RIP
; We push: dummy error code (0), interrupt number
;------------------------------------------------------------------------------
%macro ISR_NOERR 1
isr%1:
    push qword 0            ; Dummy error code
    push qword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

;------------------------------------------------------------------------------
; Macro: ISR with error code
; CPU pushes: SS, RSP, RFLAGS, CS, RIP, Error Code
; We push: interrupt number
;------------------------------------------------------------------------------
%macro ISR_ERR 1
isr%1:
    push qword %1           ; Interrupt number (error code already pushed by CPU)
    jmp isr_common_stub
%endmacro

;------------------------------------------------------------------------------
; Macro: IRQ handler
; IRQs don't push error codes
;------------------------------------------------------------------------------
%macro IRQ 2
irq%1:
    push qword 0            ; Dummy error code
    push qword %2           ; Vector number (IRQ + 32)
    jmp irq_common_stub
%endmacro

;------------------------------------------------------------------------------
; Exception ISRs (vectors 0-31)
;
; Some exceptions push an error code, some don't.
; We need to handle both cases to keep the stack frame consistent.
;------------------------------------------------------------------------------

; Exceptions without error code
ISR_NOERR 0     ; #DE Divide Error
ISR_NOERR 1     ; #DB Debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; #BP Breakpoint
ISR_NOERR 4     ; #OF Overflow
ISR_NOERR 5     ; #BR Bound Range Exceeded
ISR_NOERR 6     ; #UD Invalid Opcode
ISR_NOERR 7     ; #NM Device Not Available
ISR_ERR   8     ; #DF Double Fault (has error code, always 0)
ISR_NOERR 9     ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10    ; #TS Invalid TSS
ISR_ERR   11    ; #NP Segment Not Present
ISR_ERR   12    ; #SS Stack-Segment Fault
ISR_ERR   13    ; #GP General Protection
ISR_ERR   14    ; #PF Page Fault
ISR_NOERR 15    ; Reserved
ISR_NOERR 16    ; #MF x87 Floating-Point
ISR_ERR   17    ; #AC Alignment Check
ISR_NOERR 18    ; #MC Machine Check
ISR_NOERR 19    ; #XM/#XF SIMD Floating-Point
ISR_NOERR 20    ; #VE Virtualization
ISR_ERR   21    ; #CP Control Protection
ISR_NOERR 22    ; Reserved
ISR_NOERR 23    ; Reserved
ISR_NOERR 24    ; Reserved
ISR_NOERR 25    ; Reserved
ISR_NOERR 26    ; Reserved
ISR_NOERR 27    ; Reserved
ISR_NOERR 28    ; Hypervisor Injection
ISR_NOERR 29    ; VMM Communication
ISR_ERR   30    ; #SX Security Exception
ISR_NOERR 31    ; Reserved

;------------------------------------------------------------------------------
; Hardware IRQs (vectors 32-47)
;------------------------------------------------------------------------------
IRQ 0, 32       ; PIT Timer
IRQ 1, 33       ; Keyboard
IRQ 2, 34       ; Cascade
IRQ 3, 35       ; COM2
IRQ 4, 36       ; COM1
IRQ 5, 37       ; LPT2
IRQ 6, 38       ; Floppy
IRQ 7, 39       ; LPT1 / Spurious
IRQ 8, 40       ; RTC
IRQ 9, 41       ; ACPI
IRQ 10, 42      ; Available
IRQ 11, 43      ; Available
IRQ 12, 44      ; PS/2 Mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; Primary ATA
IRQ 15, 47      ; Secondary ATA

;------------------------------------------------------------------------------
; APIC/IPI handlers
;------------------------------------------------------------------------------
isr_apic_timer:
    push qword 0
    push qword 0xFE         ; VEC_APIC_TIMER
    jmp irq_common_stub

isr_apic_spurious:
    push qword 0
    push qword 0xFF         ; VEC_APIC_SPURIOUS
    jmp irq_common_stub

isr_ipi_reschedule:
    push qword 0
    push qword 0xF0         ; VEC_IPI_RESCHEDULE
    jmp irq_common_stub

isr_ipi_tlb:
    push qword 0
    push qword 0xF1         ; VEC_IPI_TLB_SHOOTDOWN
    jmp irq_common_stub

;------------------------------------------------------------------------------
; System call handler (int 0x80)
;------------------------------------------------------------------------------
isr_syscall:
    push qword 0
    push qword 0x80         ; VEC_SYSCALL
    jmp isr_common_stub     ; Use exception handler for now

;------------------------------------------------------------------------------
; Common ISR stub for exceptions
;
; At this point, the stack looks like:
;   [RSP+0]  = interrupt number
;   [RSP+8]  = error code (or 0)
;   [RSP+16] = RIP (pushed by CPU)
;   [RSP+24] = CS
;   [RSP+32] = RFLAGS
;   [RSP+40] = RSP (only if privilege change)
;   [RSP+48] = SS  (only if privilege change)
;
; We need to save all general-purpose registers to create a trap_frame.
;------------------------------------------------------------------------------
isr_common_stub:
    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to trap_frame as first argument
    mov rdi, rsp

    ; Call C exception handler
    call exception_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove error code and interrupt number from stack
    add rsp, 16

    ; Return from interrupt
    iretq

;------------------------------------------------------------------------------
; Common IRQ stub
;
; Same as exception stub but calls irq_handler instead.
;------------------------------------------------------------------------------
irq_common_stub:
    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to trap_frame as first argument
    mov rdi, rsp

    ; Call C IRQ handler
    call irq_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove error code and interrupt number from stack
    add rsp, 16

    ; Return from interrupt
    iretq
