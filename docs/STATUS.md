# Project Status

Snapshot date: April 20, 2026

**Snapshot**
Ocean is an educational x86_64 microkernel with a working boot path, basic kernel subsystems, and a small userspace. The kernel boots via Limine into a higher-half layout, initializes CPU, memory, scheduler, IPC, and syscalls, then starts init and an interactive shell from boot modules. This snapshot adds another pass of runtime truth and kernel cleanup: deterministic QEMU smoke, shell-smoke, and stress tooling; a fixed ring transition path for user-mode interrupts; argv-backed bootstrap `exec` stacks; tighter ELF loading checks; more honest init service reporting; and safer process teardown around exit, wait, and repeated exec.

**What Works**
- Boot and arch: Limine boot, higher-half kernel, early serial console, GDT/TSS, IDT/ISR, PIT timer, SYSCALL entry, PIC remap.
- Memory: PMM with bitmap and buddy allocator; VMM with VMAs and paging; kernel heap via slab; VMA page protections keep full 64-bit PTE flags.
- Scheduler: O(1) priority queues, preemptive tick, single-CPU only with per-CPU scaffolding, and TSS `rsp0` updates during context switch so user-mode interrupts return through a valid kernel stack.
- Processes: basic process and thread structs, fork/exec/wait path, init-child reparenting, zombie reaping, and reusable teardown for failed process setup.
- IPC: endpoints and synchronous send/recv with fast path.
- Syscall safety: user buffer/string access now goes through kernel `uaccess` helpers.
- Process lifecycle: waited children are reaped with resource cleanup, `wait()` no longer has a lost-wakeup window against child exit, and successful `exec()` tears down the old address space instead of leaking it.
- Validation tooling: `make static-check`, `make smoke`, `make shell-smoke`, `make stress`, `make compile_commands`, and CI smoke workflow.
- Syscalls: small implemented subset only; stdin/stdout over serial plus read-only `open`/`close`/`lseek`/`read` for boot modules, `exec` with argv support but no envp, and reserved syscall numbers clearly separated from the working surface.
- Userspace: minimal libc with a small in-process heap allocator, init server, shell with quoted argument parsing plus `cd`/`pwd` prompt context and module/service discovery, and small utilities with working argv startup on the bootstrap `/boot` path.

**What Is Stubbed or Simulated**
- IPC call/reply semantics, capability transfer, and cspace integration.
- Process lifecycle beyond single-thread reaping (signals, multithreaded exit edge cases).
- Init records a service plan honestly, but it does not yet spawn mem/proc/vfs/blk/filesystem/driver processes.
- Memory server, process server, VFS server, block server, and drivers are simulated and do not yet perform real kernel-mediated operations.
- Filesystem drivers and block drivers are not wired into live IPC or VFS routing.
- Boot modules load only init, shell, and a few utilities in `limine.conf`.

**Kernel-first Improvements**
- Complete IPC reply/call semantics, including reply endpoints and tracking caller context.
- Complete capability transfer and cspace enforcement for endpoints and other objects.
- Finish process lifecycle behavior beyond wait/reap (signals, multithread edge cases).
- Continue memory correctness work: page refcounting and COW teardown.
- Harden scheduler edge cases and build toward real SMP enablement.

**Secondary Improvements**
- Wire init to actually spawn services and register well-known endpoints.
- Implement real IPC request/response loops in mem, proc, vfs, and blk servers.
- Integrate filesystem drivers with VFS and block server.
- Add developer tooling: keep extending repeatable QEMU run configs and shell/runtime smoke coverage beyond the current bootstrap path.
