# Codex Development Workflow

This document describes a conservative, repeatable workflow for continuing Ocean development with Codex.

**Workflow**
1. Intake
2. Targeted inspection
3. Plan
4. Implement
5. Verify
6. Summarize

**Intake**
- Restate the goal and scope.
- Identify constraints and success criteria.
- Confirm whether to run `make` or QEMU runs.

**Targeted Inspection**
- Locate the relevant subsystem first in `kernel/`, then `servers/`, `lib/`, `drivers/`, `fs/`, and `include/`.
- Read headers and call sites before proposing changes.

**Plan**
- Propose a minimal set of files and changes.
- Call out risks and missing pieces.
- Explicitly note when behavior is simulated or stubbed.

**Implement**
- Prefer small diffs and incremental commits.
- Preserve existing code style and conventions.
- Avoid cross-cutting refactors unless requested.

**Verify**
- Default to no build or QEMU runs unless asked.
- Run `make` when explicitly requested or when the change is large and the user approves.

**Summarize**
- Describe what changed and why.
- Point to key files.
- Note any follow-up risks or gaps.

**Validation Matrix**

| Change Type | Default Action | When to Run `make` | When to Run QEMU |
| --- | --- | --- | --- |
| Documentation-only | No build | Only on request | Only on request |
| Kernel changes | Analyze first | With explicit approval | Only on request |
| Userspace server changes | Analyze first | With explicit approval | Only on request |
| Boot configuration changes | Analyze first | With explicit approval | Only on request |

**Change Checklists**

New syscall
- Add number and documentation in `kernel/include/ocean/syscall.h` and `lib/libocean/include/ocean/syscall.h`.
- Implement handler in `kernel/syscall/dispatch.c`.
- Add user pointer validation if user memory is touched.
- Update any userspace wrappers in `lib/libocean`.

IPC protocol change
- Update `include/ocean/ipc_proto.h`.
- Update server implementations that use the protocol.
- Check kernel IPC fast path or endpoint semantics if changed.

New server or driver
- Add source under `servers/` or `drivers/` or `fs/`.
- Add build rules in `user.mk`.
- Decide if it should be loaded as a boot module in `limine.conf`.
- Register well-known endpoints if applicable.

Boot module changes
- Update `limine.conf` to add or remove modules.
- Ensure `Makefile` copies the module into the ISO.

**Failure Handling**
- If the toolchain is missing, report the exact missing component and reference `tools/setup-toolchain.sh`.
- If QEMU or xorriso is missing, explain the limitation and continue with code changes.
- If a change cannot be verified locally, document the gap and a suggested command for the user to run.
