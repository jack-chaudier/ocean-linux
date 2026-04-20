# Codex Guidance for Ocean

This file defines the preferred workflow and conventions for working in this repo with Codex.

**Repo Map**
- `kernel/`: core kernel code, arch setup, memory, scheduler, IPC, syscalls.
- `servers/`: userspace servers like init, proc, mem, vfs, blk, sh.
- `lib/`: libc and shared userspace libs.
- `include/`: shared headers and IPC protocol definitions.
- `drivers/`: userspace device drivers.
- `fs/`: filesystem drivers.

**Build and Run**
- `make` builds the kernel and ISO.
- `make run` runs QEMU with serial output.
- `make debug` runs QEMU with GDB server on `:1234`.
- `make info` prints build configuration.
- `make compile_commands` generates `compile_commands.json`.

**Codex Guardrails**
- Default to analysis and small diffs.
- Do not run `make` or QEMU unless the user asks or approves.
- Update `docs/STATUS.md` when architecture or subsystem behavior changes.

**Conventions**
- C11 with GNU extensions.
- 4-space indentation.
- Kernel code avoids libc; use `kprintf` for kernel logging.
- Userspace code uses the minimal libc in `lib/libc`.
- Shared APIs go in `include/` headers.

**Toolchain Notes**
- Preferred toolchain: `x86_64-elf-gcc`.
- If missing, use `tools/setup-toolchain.sh` or install a cross-compiler.
- QEMU and xorriso are needed for ISO testing.

**Further Reading**
- `docs/CODEX_WORKFLOW.md`
- `docs/STATUS.md`
