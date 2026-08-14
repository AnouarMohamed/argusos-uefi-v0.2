# ArgusOS userspace boundary v0.15

ArgusOS v0.15 introduces the first real kernel/user privilege boundary. Two
embedded position-independent probe images run at x86-64 ring 3, use private page
tables, cross into the kernel with `SYSCALL`, yield cooperatively, resume with
their own saved state, and exit. This milestone deliberately validates the
mechanism before treating the existing desktop utilities as user applications.

## Address spaces

Each process owns a new PML4. Supervisor-only kernel mappings are copied into the
root so the kernel can service a syscall without changing CR3 first. User mappings
live in an otherwise unused lower-half PML4 slot and carry the user-accessible bit
at every page-table level.

The two probes use identical virtual addresses but distinct physical pages. Each
address space contains:

- one read-only executable code page;
- one writable, non-executable stack page when NX is supported;
- one deliberately unmapped guard page below the stack.

The boot self-test verifies distinct page-table roots and backing pages, identical
virtual layouts, code/stack permissions, the guard gap, and that an identity-mapped
kernel physical address is not user-accessible.

## Privilege transition

The GDT now provides kernel code/data and user data/code selectors plus the TSS.
Ring-3 entry uses `IRETQ`. `IA32_STAR`, `IA32_LSTAR`, `IA32_FMASK`, and EFER.SCE
configure the fast syscall path. The TSS RSP0 value and syscall entry both point at
a dedicated 32 KiB kernel stack.

`SYSCALL` does not switch stacks itself, so the assembly entry captures the user
register context before moving to the kernel stack. Return-type syscalls restore
the context with `SYSRETQ`. Yield and exit return to the C scheduler, which restores
the kernel call frame and interrupt state before selecting another process.

## User ABI v1

The syscall number is passed in `RAX`. The Argus user ABI passes the current call
arguments in `RDI` and `RSI`, and returns results in `RAX`; it is intentionally
separate from the Microsoft x64 C calling convention used inside the EFI image.

| Number | Operation | Arguments | Result |
| --- | --- | --- | --- |
| 1 | write | `RDI` buffer, `RSI` byte count | bytes written or `UINT64_MAX` |
| 2 | getpid | none | process ID |
| 3 | yield | none | zero after resume |
| 4 | exit | `RDI` status | does not resume |

`write` is serial-only, rejects empty or greater-than-128-byte requests, checks
overflow, and validates every covered user page before reading it. There is no
direct user access to framebuffer, devices, allocator state, or kernel pages.

## Scheduler proof

The boot scheduler is cooperative and round-robin. Probe 1 starts and yields,
probe 2 starts and yields, then each resumes and exits. A private value stored on
each user stack is checked after the yield. The self-test requires exactly two
successful processes, ten syscalls, two yields, four writes, and four kernel/user
context switches.

The `processes` kernel-shell command exposes these counters and ends with
`USERSPACE_STATUS_OK` when the validated process boundary is online.

## Deliberate limits

This is not yet a general process service. User images are statically embedded,
created only during boot, and expected not to fault. There is no ELF loader,
dynamic spawn, preemption, user exception recovery, IPC, shared memory, file
descriptors, or user display protocol. The retained desktop and its utility apps
still run in the kernel.

The next language milestone is v0.16: a minimal freestanding C++ user image. A
persistent C++ display server follows only after scheduling, IPC/shared surfaces,
and loading are strong enough to support a long-lived service.
