# Ocean Architecture v2 (In-Progress)

## Goals
- Keep kernel minimal while hardening safety boundaries.
- Move from ad hoc pointer handling to explicit validated user-memory access.
- Establish deterministic validation gates for every kernel/userspace change.

## Kernel Boundaries
1. Syscall boundary
- All user pointers must flow through `uaccess` helpers.
- Syscall handlers return negative errno-style values for failure.

2. IPC boundary
- Endpoints are refcounted objects with explicit dead/listed state.
- Endpoint destruction is asynchronous with respect to outstanding references.
- Wait queue nodes are allocation-backed and not stack-persistent across sleep.

3. Scheduler boundary
- Channel sleep/wakeup is implemented via global thread registry.
- Sleepers are woken by channel identity and transitioned through scheduler APIs only.

4. Process boundary
- Parent/child wait semantics include real reaping and resource teardown.
- Children are reparanted to init on parent exit.

5. Memory boundary
- VMM page accounting tracks actual mapping/unmapping.
- Slab and page allocator interaction uses page flags (`PG_SLAB`, compound head/order) to free correctly.

## Validation Contract
- `make all-user`: compile kernel + userspace + ISO.
- `make smoke`: deterministic boot + init signatures, panic/fault signature scan.
- `make stress`: repeated smoke cycles.
- CI (`.github/workflows/ci.yml`) runs build + smoke and archives serial logs.

## Current Non-Goals
- Full SMP correctness and per-CPU scheduler isolation.
- Full capability transfer/cspace enforcement.
- Fully wired production-grade VFS/block/filesystem pipeline.
