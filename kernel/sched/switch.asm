;
; Ocean Kernel - Context Switch
;
; Low-level context switch implementation for x86_64.
; Saves callee-saved registers and switches stack pointer.
;

section .text

global switch_context
global ret_from_fork

;
; switch_context(prev_context, next_context)
;
; Arguments:
;   rdi = pointer to prev thread's cpu_context
;   rsi = pointer to next thread's cpu_context
;
; cpu_context structure:
;   +0   r15
;   +8   r14
;   +16  r13
;   +24  r12
;   +32  rbx
;   +40  rbp
;   +48  rsp
;   +56  rip (return address)
;
switch_context:
    ; Save callee-saved registers to prev context
    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbx
    mov [rdi + 40], rbp
    mov [rdi + 48], rsp

    ; Save return address (where we'll resume)
    mov rax, [rsp]          ; Get return address from stack
    mov [rdi + 56], rax     ; Save as RIP in context

    ; Load next context
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbx, [rsi + 32]
    mov rbp, [rsi + 40]
    mov rsp, [rsi + 48]

    ; Push the return address and ret to it
    mov rax, [rsi + 56]     ; Get RIP from context
    push rax
    ret                     ; "Return" to the new thread

;
; ret_from_fork
;
; Entry point for newly forked processes.
; Child returns from fork with 0 in rax.
;
; When we get here (via switch_context):
;   r12 = kernel stack top address (set in process_fork)
;
; The syscall frame layout on kernel stack (from top):
;   -8:   SS
;   -16:  user RSP
;   -24:  user RFLAGS
;   -32:  CS
;   -40:  user RIP
;   -48:  error_code
;   -56:  int_no
;   -64:  RAX (syscall number / return value)
;   -72:  RBX
;   -80:  RCX
;   -88:  RDX
;   -96:  RSI
;   -104: RDI
;   -112: RBP
;   -120: R8
;   -128: R9
;   -136: R10
;   -144: R11
;   -152: R12
;   -160: R13
;   -168: R14
;   -176: R15  <-- saved registers start here
;
ret_from_fork:
    ; Set RSP to point to saved r15 (start of general regs)
    ; r12 contains kernel stack top, frame is at r12 - 176
    lea rsp, [r12 - 176]

    ; Set RAX (return value) to 0 for child
    ; RAX is at offset 14*8 = 112 from r15
    mov qword [rsp + 112], 0

    ; Restore all registers (same order as syscall_entry_simple)
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax                     ; Fork return value = 0

    ; Skip error code and int_no
    add rsp, 16

    ; Pop the iret frame for SYSRET
    ; Stack: RIP, CS, RFLAGS, RSP, SS
    pop rcx                     ; User RIP -> RCX (for SYSRET)
    add rsp, 8                  ; Skip CS
    pop r11                     ; User RFLAGS -> R11 (for SYSRET)
    pop rsp                     ; User RSP

    ; Switch back to user GS base
    swapgs

    ; Return to user mode
    o64 sysret
