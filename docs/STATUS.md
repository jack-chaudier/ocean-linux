# Project Status

Snapshot date: April 20, 2026

**Snapshot**
Ocean is an educational x86_64 microkernel with a working boot path, basic kernel subsystems, and a small userspace. The kernel boots via Limine into a higher-half layout, initializes CPU, memory, scheduler, IPC, and syscalls, then starts init and the shell from boot modules. This snapshot includes significant kernel safety hardening: centralized user-pointer validation, improved endpoint lifetime management, functional channel wakeups, deterministic QEMU smoke/stress tooling, a portable `make static-check` path for host-side verification, a minimal read-only `/boot` file-descriptor surface for boot modules, and cleaner process teardown around failed fork/exec setup.

**What Works**
- Boot and arch: Limine boot, higher-half kernel, early serial console, GDT/TSS, IDT/ISR, PIT timer, SYSCALL entry, PIC remap.
- Memory: PMM with bitmap and buddy allocator; VMM with VMAs and paging; kernel heap via slab; VMA page protections keep full 64-bit PTE flags.
- Scheduler: O(1) priority queues, preemptive tick, single-CPU only with per-CPU scaffolding.
- Processes: basic process and thread structs, fork/exec/wait path, kernel threads, and reusable teardown for failed process setup.
- IPC: endpoints and synchronous send/recv with fast path.
- Syscall safety: user buffer/string access now goes through kernel `uaccess` helpers.
- Process lifecycle: waited children are reaped with resource cleanup.
- Validation tooling: `make static-check`, `make smoke`, `make stress`, and CI smoke workflow.
- Syscalls: minimal set; stdin/stdout over serial plus read-only `open`/`close`/`lseek`/`read` for boot modules.
- Userspace: minimal libc with a small in-process heap allocator, init server, shell with quoted argument parsing plus `cd`/`pwd` prompt context and module/service discovery, and small utilities with clearer boot-module behavior.

**What Is Stubbed or Simulated**
- IPC call/reply semantics, capability transfer, and cspace integration.
- Process lifecycle beyond reaping (signals, multithreaded exit edge cases).
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
- Add developer tooling: repeatable QEMU run configs, compile_commands generation in CI, and deeper runtime smoke tests once the full toolchain is present.
