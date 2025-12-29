# Ocean Microkernel

An educational x86_64 microkernel written in C, designed to explore operating system concepts and eventually become self-hosting.

## Overview

Ocean is a Unix-inspired microkernel where only essential services run in kernel space:
- Memory management (physical and virtual)
- Process/thread scheduling
- Inter-process communication (IPC)
- Basic hardware abstraction

Everything else—filesystems, networking, device drivers—runs as userspace servers communicating via message passing.

## Features

### Implemented
- **Boot**: Limine bootloader protocol, serial console output
- **CPU**: GDT/TSS, IDT with exception handlers, PIC remapping
- **Memory**: Buddy allocator for physical memory, 4-level paging, kernel heap (slab allocator)
- **Processes**: Process/thread creation, context switching, ELF loading
- **Scheduler**: Priority-based preemptive scheduler with per-CPU run queues
- **Syscalls**: SYSCALL/SYSRET fast path, basic syscall interface
- **IPC**: Synchronous message passing with endpoints, capability-based design
- **Userspace**: C runtime, minimal libc, init server

### Planned
- Core userspace servers (memory, process, VFS)
- Storage stack (block device server, filesystem drivers)
- Full POSIX-like userspace

## Building

### Prerequisites

- x86_64 cross-compiler (`x86_64-elf-gcc`) or native x86_64 GCC
- NASM assembler
- xorriso (for ISO creation)
- QEMU (for testing)

On macOS with Homebrew:
```bash
brew install x86_64-elf-gcc nasm xorriso qemu
```

On Debian/Ubuntu:
```bash
sudo apt install gcc-x86-64-linux-gnu nasm xorriso qemu-system-x86
```

### Build Commands

```bash
# Build bootable ISO
make

# Run in QEMU
make run

# Run with GDB server for debugging
make debug

# Clean build artifacts
make clean

# Show build configuration
make info
```

## Project Structure

```
ocean-linux/
├── kernel/                     # Core microkernel
│   ├── arch/x86_64/           # Architecture-specific code
│   │   ├── boot/              # Limine interface, early init
│   │   ├── cpu/               # GDT, IDT, TSS
│   │   ├── interrupt/         # ISR stubs, IRQ handling, timer
│   │   ├── mm/                # Page table manipulation
│   │   └── syscall/           # SYSCALL entry point
│   ├── mm/                    # Memory management
│   ├── proc/                  # Process/thread management
│   ├── sched/                 # Scheduler
│   ├── ipc/                   # IPC subsystem
│   ├── syscall/               # Syscall handlers
│   ├── lib/                   # Kernel library (printf, string)
│   └── include/ocean/         # Kernel headers
├── servers/                   # Userspace servers
│   └── init/                  # Init process (PID 1)
├── lib/                       # Shared libraries
│   ├── libc/                  # Minimal C library
│   └── libocean/              # Ocean syscall wrappers
├── kernel.ld                  # Kernel linker script
├── user.ld                    # Userspace linker script
├── limine.conf                # Bootloader configuration
└── Makefile                   # Build system
```

## Architecture

### Memory Layout

```
Virtual Address Space (48-bit canonical, 4-level paging)

USER SPACE: 0x0000000000000000 - 0x00007FFFFFFFFFFF
  0x0000000000400000   Program text/data
  0x00007FFFFFFFE000   User stack (grows down)

KERNEL SPACE: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
  0xFFFF800000000000   Direct physical mapping (HHDM)
  0xFFFF880000000000   Kernel heap
  0xFFFFFFFF80000000   Kernel text/data
```

### IPC Model

Ocean uses synchronous message passing:
- Messages up to 64 bytes transferred via registers (fast path)
- Larger messages use shared memory grants
- Capability-based access control for endpoints

### Boot Sequence

1. Limine loads kernel at 0xFFFFFFFF80000000
2. Kernel initializes CPU (GDT, IDT, interrupts)
3. Memory manager sets up physical/virtual memory
4. Scheduler and IPC subsystems initialize
5. Init process loaded from boot module
6. Init starts core system servers

## Development

### Running Tests

```bash
# Run in QEMU with serial output
make run

# Debug with GDB
make debug
# In another terminal:
gdb build/kernel.elf -ex "target remote :1234"
```

### Code Style

- C11 with GNU extensions
- 4-space indentation
- Kernel code in `kernel/`, userspace in `servers/` and `lib/`
- Headers use `#ifndef _OCEAN_*_H` guards

## License

This project is for educational purposes. See individual source files for licensing information.

## Acknowledgments

- [Limine](https://github.com/limine-bootloader/limine) bootloader
- [OSDev Wiki](https://wiki.osdev.org/) for x86_64 documentation
- Minix 3 for microkernel design inspiration
