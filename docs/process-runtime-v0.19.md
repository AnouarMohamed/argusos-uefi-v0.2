# ArgusOS process runtime v0.19

ArgusOS v0.19 completes two process milestones: validated ELF application
loading with explicit lifecycle and fault containment, followed by blocking
events and timer preemption. These are security prerequisites for networking and
untrusted document rendering. They are not a browser or a claim that arbitrary
third-party programs are safe to run.

## Validated ELF loading

The three graphical applications are now embedded as complete ELF64 images.
The kernel parses their program headers rather than copying a flat text blob.
An image is rejected before address-space creation unless all of these rules
hold:

- ELF64, little-endian, current version, `ET_EXEC`, x86-64
- bounded header table with at most eight load segments and sixteen image pages
- page-aligned load offsets and virtual addresses inside the application range
- readable segments only, with writable and executable permissions mutually
  exclusive
- file size no greater than memory size, with checked offset and address sums
- no overlapping mapped pages
- entry point contained in an executable segment
- non-executable GNU stack declaration
- no interpreter, dynamic linker, TLS, or unrecognized program-header contract

The loader zeroes every physical page before copying file bytes. It maps code
read-only and executable, writable data NX, the stack writable and NX, and
leaves the page below the stack unmapped. Each application keeps its private
CR3 and private indexed surface mapping.

Images remain compiled into the EFI payload for this milestone. The loader
boundary is real, but loading from a signed filesystem package is a later step.

## Lifecycle

PIDs are assigned when an instance starts and are no longer hardcoded into the
C++ applications. Fixed app IDs identify Snake, Calculator, and Notes while PID
values change after a restart.

The kernel shell exposes the lifecycle operations below:

```text
appstart snake|calc|notes
appstop snake|calc|notes
apprestart snake|calc|notes
```

Stopping releases the private page tables, image pages, stack, and shared
surface. Starting validates and maps the original ELF image into a fresh
address space. A stopped, exited, or faulted application is shown using that
explicit state instead of stale pixels.

## Contained user faults

Interrupt frames now retain the privilege-transition stack fields. If an x86
exception originates at ring 3, the kernel captures the complete integer
context, vector, error code, instruction pointer, and page-fault address where
applicable. It marks that process faulted and returns to the kernel scheduler.
The exception is never resumed and does not panic the machine.

Exceptions originating at ring 0 still enter the existing fatal diagnostic
path. A boot probe executes an invalid opcode at ring 3 and must be terminated
while kernel startup continues.

## Blocking events and preemption

User ABI v2 adds syscall 8, an event wait with an absolute APIC tick deadline.
`UINT64_MAX` means input only. Input arrival or deadline expiry moves the process
from waiting to ready. Calculator and Notes sleep until focused input arrives.
Snake sleeps until input or its next movement deadline.

The local APIC timer can now interrupt ring-3 code, save its full integer
context, and return control to the kernel. A boot probe that never issues a
syscall must be preempted before startup is allowed to continue. This prevents a
CPU-bound or stuck user process from permanently taking the machine.

The current scheduler remains single-core and intentionally small. It has no
threads, priorities, SMP synchronization, signal delivery, or general IPC.
