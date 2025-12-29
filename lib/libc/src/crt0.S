/*
 * Ocean C Runtime - Entry Point (crt0)
 *
 * This is the true entry point for user programs.
 * It sets up the environment and calls main().
 *
 * On entry from kernel:
 *   - RSP points to user stack
 *   - Stack contains: argc, argv[], envp[], auxv[]
 *   - All registers are cleared except RSP
 */

.section .text
.global _start
.type _start, @function

_start:
    /* Clear frame pointer for backtraces */
    xor     %rbp, %rbp

    /*
     * Stack layout on entry (set up by exec):
     *   [RSP + 0]  = argc
     *   [RSP + 8]  = argv[0]
     *   ...
     *   [RSP + 8 + 8*argc] = NULL (end of argv)
     *   [RSP + ...] = envp[0]
     *   ...
     *   NULL (end of envp)
     *   auxv (auxiliary vector)
     *
     * For now, we'll use a simplified layout:
     *   argc = 0, argv = NULL, envp = NULL
     */

    /* Get argc from stack (or use 0 if not set up) */
    mov     (%rsp), %rdi        /* argc -> first argument */

    /* Calculate argv pointer */
    lea     8(%rsp), %rsi       /* argv -> second argument */

    /* Calculate envp pointer (skip argc + argv + NULL) */
    /* For simplicity, just pass NULL for now */
    xor     %rdx, %rdx          /* envp = NULL */

    /* Align stack to 16 bytes (required by ABI) */
    and     $-16, %rsp

    /* Call main(argc, argv, envp) */
    call    main

    /* main() returned - call exit with return value */
    mov     %rax, %rdi          /* Return value -> first argument */
    call    exit

    /* If exit returns (it shouldn't), loop forever */
1:
    hlt
    jmp     1b

.size _start, . - _start


/*
 * Alternate entry point with no arguments
 * Used for simple test programs
 */
.global _start_simple
.type _start_simple, @function

_start_simple:
    xor     %rbp, %rbp
    and     $-16, %rsp

    /* Call main with argc=0, argv=NULL */
    xor     %rdi, %rdi
    xor     %rsi, %rsi
    xor     %rdx, %rdx
    call    main

    mov     %rax, %rdi
    call    exit

1:  hlt
    jmp     1b

.size _start_simple, . - _start_simple
