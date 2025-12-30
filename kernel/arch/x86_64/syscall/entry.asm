;
; Ocean Kernel - System Call Entry
;
; SYSCALL/SYSRET entry point for x86_64.
;
; On SYSCALL:
;   RCX = user RIP (return address)
;   R11 = user RFLAGS
;   RAX = syscall number
;   RDI, RSI, RDX, R10, R8, R9 = arguments 1-6
;

section .text

global syscall_entry_simple
global syscall_return_to_user
global enter_usermode
global enter_usermode_from_syscall

extern syscall_dispatch

;
; syscall_entry_simple - Main SYSCALL entry point
;
; Called by hardware when user executes SYSCALL.
; CPU has already:
;   - Loaded CS from STAR[47:32]
;   - Saved RIP to RCX
;   - Saved RFLAGS to R11
;   - Cleared RFLAGS per SFMASK
;
syscall_entry_simple:
    ; We're now in kernel mode, but still on user stack!
    ; Switch to kernel stack using SWAPGS to access per-CPU data

    swapgs                          ; Get kernel GS base

    ; Save user RSP to per-CPU storage (offset 0)
    mov [gs:0], rsp

    ; Load kernel stack from per-CPU storage (offset 8)
    mov rsp, [gs:8]

    ; Build a trap frame on kernel stack
    ; Save user context for SYSRET

    ; Stack segment and user RSP
    push qword 0x23                 ; SS (user data: 0x20 | RPL 3)
    push qword [gs:0]               ; User RSP

    ; User RFLAGS and code segment
    push r11                        ; User RFLAGS (saved by CPU)
    push qword 0x2b                 ; CS (user code 64-bit: 0x28 | RPL 3)
    push rcx                        ; User RIP (saved by CPU)

    ; Fake interrupt/error codes
    push 0                          ; Error code
    push 0x80                       ; "Interrupt" number (syscall)

    ; Save all general purpose registers
    push rax                        ; RAX (syscall number)
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Prepare arguments for syscall_dispatch
    ; C calling convention: RDI, RSI, RDX, RCX, R8, R9
    ; We have: RAX=nr, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5, R9=a6

    ; Save original arg values before we clobber them
    mov r12, rdi                    ; Save arg1
    mov r13, rsi                    ; Save arg2
    mov r14, rdx                    ; Save arg3

    ; Set up C call arguments
    mov rdi, rax                    ; arg0 = syscall number
    mov rsi, r12                    ; arg1
    mov rdx, r13                    ; arg2
    mov rcx, r14                    ; arg3
    mov r8, r10                     ; arg4 (was in R10 for syscall)
    ; r9 already has arg5
    ; arg6 would go on stack but we don't use 7-arg syscalls

    ; Align stack to 16 bytes (required by ABI)
    sub rsp, 8

    ; Call the C dispatcher
    call syscall_dispatch

    ; Restore stack alignment
    add rsp, 8

    ; Store return value in the saved RAX slot
    ; RAX slot is at offset [rsp + 14*8] after the general regs
    mov [rsp + 14*8], rax

    ; Fall through to return path

.syscall_return:
    ; Restore all registers
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
    pop rax                         ; Return value

    ; Skip error code and int_no
    add rsp, 16

    ; Pop the iret frame into SYSRET registers
    ; Stack layout: RIP, CS, RFLAGS, RSP, SS
    pop rcx                         ; User RIP -> RCX (for SYSRET)
    add rsp, 8                      ; Skip CS
    pop r11                         ; User RFLAGS -> R11 (for SYSRET)
    pop rsp                         ; User RSP (skip SS, not needed for SYSRET)

    ; Switch back to user GS base
    swapgs

    ; Return to user mode
    ; SYSRET: RCX -> RIP, R11 -> RFLAGS
    ; o64 prefix ensures 64-bit mode
    o64 sysret


;
; syscall_return_to_user - Return to user mode with specified entry and stack
;
; Used when first entering user mode (e.g., exec).
;
; RDI = user entry point (RIP)
; RSI = user stack pointer (RSP)
;
syscall_return_to_user:
    ; Set up for SYSRET
    mov rcx, rdi                    ; User RIP -> RCX
    mov rsp, rsi                    ; User RSP
    mov r11, 0x202                  ; RFLAGS: IF enabled, reserved bit

    ; Switch to user GS
    swapgs

    ; Return to user mode
    o64 sysret


;
; enter_usermode - Enter user mode using IRET
;
; More robust method for initial entry to user mode.
; Sets up a complete interrupt frame and uses IRETQ.
;
; NOTE: This is for FIRST entry to user mode from kernel.
; We do NOT swapgs here because:
;   - Kernel has MSR_GS_BASE = 0, MSR_KERNEL_GS_BASE = &percpu
;   - When user calls SYSCALL, swapgs will swap to kernel GS
;   - If we swapped here, we'd mess up the GS bases
;
; RDI = user RIP
; RSI = user RSP
; RDX = user RFLAGS (or 0 for default: 0x202)
;
enter_usermode:
    ; Use default RFLAGS if not specified
    test rdx, rdx
    jnz .has_flags
    mov rdx, 0x202                  ; IF enabled, reserved bit set
.has_flags:

    ; Build interrupt return frame on current stack
    push qword 0x23                 ; SS (user data: 0x20 | RPL 3)
    push rsi                        ; RSP
    push rdx                        ; RFLAGS
    push qword 0x2b                 ; CS (user code 64-bit: 0x28 | RPL 3)
    push rdi                        ; RIP

    ; Clear all general purpose registers for clean start
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; Don't swapgs - we're entering user mode for the first time from kernel init
    ; GS base for user will be 0 (or whatever MSR_GS_BASE is)
    ; MSR_KERNEL_GS_BASE has &percpu for when SYSCALL happens

    ; Return to user mode
    iretq

;
; enter_usermode_from_syscall - Enter user mode from syscall context
;
; Same as enter_usermode but does swapgs because we're in a swapped state
; from handling a syscall (like exec).
;
; RDI = user RIP
; RSI = user RSP
; RDX = user RFLAGS (or 0 for default: 0x202)
;
enter_usermode_from_syscall:
    ; Use default RFLAGS if not specified
    test rdx, rdx
    jnz .has_flags2
    mov rdx, 0x202                  ; IF enabled, reserved bit set
.has_flags2:

    ; Build interrupt return frame on current stack
    push qword 0x23                 ; SS (user data: 0x20 | RPL 3)
    push rsi                        ; RSP
    push rdx                        ; RFLAGS
    push qword 0x2b                 ; CS (user code 64-bit: 0x28 | RPL 3)
    push rdi                        ; RIP

    ; Clear all general purpose registers for clean start
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; Swap GS bases - we were in syscall context with kernel GS
    swapgs

    ; Return to user mode
    iretq
