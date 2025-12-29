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
ret_from_fork:
    xor eax, eax            ; Return 0 (child's fork return value)
    ; Fall through to return to user space (future syscall return)
    ret
