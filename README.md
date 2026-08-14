# ArgusOS UEFI Study Kernel v0.7

A deliberately small x86-64 UEFI bare-metal study kernel.

This is the practical successor to the 16-bit BIOS ArgusOS shell. It boots on
modern UEFI firmware as `EFI/BOOT/BOOTX64.EFI` and begins with a small firmware
monitor. The `boot` command captures a final memory map, calls
`ExitBootServices()`, switches to an Argus-owned stack, and enters a freestanding
kernel with framebuffer and direct COM1 output.

## What is real here?

- The firmware loads your PE/COFF executable directly.
- There is no Windows/Linux/DOS underneath it.
- `cpu_vendor`, `cpu_cpuid1`, and `cpu_read_tsc` are handwritten x86-64 assembly.
- `mem`/`memmap` inspect the actual UEFI physical memory map.
- `reboot` and `shutdown` invoke firmware runtime services.
- The shell, parsing, integer formatting, and console logic have no libc dependency.
- GOP is located at boot and Argus writes glyph pixels directly into the framebuffer.
- The framebuffer console implements clear, cursor movement, backspace, wrapping, and scrolling itself.
- `boot` performs the final UEFI handoff without using Boot Services afterward.
- The post-firmware kernel runs on an explicitly allocated 64 KiB stack.
- A bitmap physical-page allocator owns conventional RAM, protects permanent reservations,
  and can allocate contiguous page runs.
- A coalescing kernel heap provides 16-byte-aligned `kmalloc`/`kfree` allocations.
- The physical and heap allocators run self-tests on every kernel entry.
- COM1 output and input remain available after firmware services are gone.
- Kernel-owned page tables enable write protection and NX where possible.
- An Argus GDT/IDT handles all architectural CPU exceptions and dispatches
  registered external interrupt handlers.
- ACPI XSDT/MADT discovery locates processors and interrupt controllers.
- The legacy PIC is masked and a periodic local-APIC timer supplies interrupts.
- A native PS/2 Set-1 keyboard decoder handles normal keys, Shift, Caps Lock,
  Enter, Backspace, and Tab without firmware services.
- A post-firmware kernel monitor accepts commands through COM1 or PS/2 while
  rendering to the framebuffer and serial terminal.
- A versioned, validated C module ABI hosts one statically linked `no_std` Rust
  checksum module without giving it boot, allocator, interrupt, or device ownership.
- A dependency-free Python host tool creates test media and drives interactive
  QEMU smoke tests through the native serial shell.

## What is *not* implemented yet?

The pre-boot monitor still uses UEFI keyboard input. The post-boot monitor owns
its input path, but USB keyboards are not supported yet; physical machines whose
firmware does not expose a PS/2-compatible keyboard will need an xHCI/USB HID
driver. There is no scheduler, filesystem, userspace, networking, or SMP startup.
Runtime Services memory is preserved, but the kernel does not call Runtime
Services after the handoff.

## Build

Requires Python 3.10+, Clang 17-ish, `lld-link`, and Rust 1.97.1 with the
`x86_64-pc-windows-msvc` target. The checked-in `rust-toolchain.toml` configures
Rust automatically when `rustup` is available:

```sh
make
```

Output:

```text
build/BOOTX64.EFI
```

CI performs the same freestanding build with warnings treated as errors, creates
a FAT32 boot image, boots it with QEMU/OVMF, enters `boot`, and requires explicit
post-firmware, allocator, paging, IDT, APIC-timer, and kernel-shell markers. It
then drives the native COM1 input path and verifies `status`, `modules`, and
`alloc 4096`.
The media creation, OVMF discovery, serial synchronization, and shell probes are
implemented by `tools/argus.py` instead of inline CI shell/Expect logic.

Useful host targets:

```sh
make host-check  # syntax-check the Python host tool
make test-image  # create build/argus-test.img
make smoke       # build, boot QEMU/OVMF, and probe the native shell
```

To create the removable-media directory structure:

```sh
make usb-tree
```

which produces:

```text
EFI/BOOT/BOOTX64.EFI
```

## Boot on a modern HP / normal UEFI PC

Use a spare USB stick formatted as FAT32. Copy the directory `EFI` to the root of the USB so the file is exactly:

```text
USB:/EFI/BOOT/BOOTX64.EFI
```

Then enter the firmware boot menu and choose the UEFI USB device. You will normally need Secure Boot disabled because this educational EFI binary is unsigned.

**Do not overwrite your internal Windows/Linux disk.** This project does not need `dd`; ordinary file-copying to a FAT32 USB is enough.

Firmware varies between HP models, so test in QEMU/OVMF before physical hardware when possible.

## Pre-boot commands

```text
help
about
clear
firmware
cpu
tsc
time
mem
memmap
boot
bootfault
color 14
video
echo hello
reboot
shutdown
exit
```

After `boot`, the firmware-backed monitor is replaced by the native kernel
monitor:

```text
help
status
mem
heap
heaptest
alloc 4096
modules
ticks
input
clear
echo hello
fault
halt
```

## Source map

```text
src/efi.h       minimal UEFI ABI/types/protocol structures
src/main.c      pre-boot monitor + UEFI time/runtime interaction
src/boot.c      final memory-map capture + ExitBootServices handoff
src/boot_info.h immutable firmware-to-kernel handoff contract
src/kernel.c    post-firmware kernel entry and subsystem initialization
src/pmm.c       tracked physical-page and contiguous-run allocator
src/heap.c      aligned first-fit kernel heap with split/coalesce support
src/paging.c    kernel-owned identity page tables and memory attributes
src/acpi.c      validated RSDP/XSDT/MADT discovery
src/arch.c      GDT, IDT, exception/external dispatch, and legacy PIC masking
src/apic.c      xAPIC/x2APIC activation and periodic local timer
src/serial.c    direct 16550/COM1 output and nonblocking input
src/ps2.c       native PS/2 Set-1 keyboard decoder
src/input.c     unified nonblocking COM1/PS2 input selection
src/kconsole.c  post-firmware framebuffer/serial console
src/kernel_shell.c native post-firmware command monitor
src/module_abi.h versioned cross-language descriptor and function contract
src/module.c     C-side module validation, registry, and boot self-test
src/rust_probe.rs bounded no_std checksum module behind ABI v1
src/uefi_memory.c reusable memory-map allocation and validation
src/gop.c       GOP discovery and raw framebuffer pixel operations
src/console.c   Argus framebuffer terminal + UEFI text fallback
src/font5x7.c   tiny built-in 5x7 terminal font
src/cpu.S       CPU instructions, stack switch, and interrupt entry stubs
Makefile        freestanding PE/COFF build
tools/argus.py  FAT image creation and interactive QEMU smoke runner
docs/module-abi-v1.md ownership, layout, failure, and versioning contract
```

## Language policy

- C owns the kernel, allocators, parsers, consoles, and early drivers.
- x86-64 assembly is limited to instructions and transitions C cannot express safely.
- Make coordinates the host build while Python owns stateful test orchestration.
- Python owns image construction and QEMU test orchestration on the host; it is
  not part of the boot image.
- Rust is restricted to bounded `no_std` modules behind the documented C ABI.
  The first module is a stateless checksum probe with no allocator or hardware access.
- Boot, page tables, allocators, interrupt control, and device ownership remain
  in C and x86-64 assembly until a later ABI explicitly and safely exposes them.
- Do not add languages merely by subsystem count: every language adds a
  compiler, runtime, ABI, debugging, and CI maintenance surface.

## Study progression

### Stage A — completed in v0.2
Learn:

- Microsoft x64 / UEFI calling convention
- PE/COFF EFI entry points
- CPUID and RDTSC
- firmware memory descriptors
- freestanding C/assembly interoperability

### Stage B — completed in v0.3: own framebuffer terminal
Implemented:

- GOP protocol discovery
- direct linear-framebuffer pixel writes
- built-in bitmap font rendering
- cursor/backspace/wrapping
- scrolling
- UEFI text-console fallback if GOP is unavailable

The framebuffer layer validates its mode, pitch, pixel masks, and backing-buffer
size before writing.

### Stages C/D — completed in v0.4: memory ownership and firmware exit

Implemented:

- parse memory map
- allocate and protect boot information, page bitmaps, and a kernel stack
- preserve the loaded image and final memory-map storage
- retry the final memory-map/key sequence when required
- call `ExitBootServices()` and stop using firmware protocols
- switch stacks and enter `kernel_main()`
- allocate/free physical pages with a boot-time self-test

### Stage E — completed in v0.5: exceptions, paging, and interrupts

Implemented:

- kernel GDT and IDT with normalized assembly exception frames
- fatal exception reports including error code, RIP, and CR2 for page faults
- identity page tables using 2 MiB leaves, MMIO cache-disable flags, CR0.WP, and NX
- validated ACPI RSDP/XSDT and MADT parsing
- legacy PIC masking
- xAPIC/x2APIC activation and a verified periodic timer interrupt

Use `bootfault` to enter the kernel and deliberately raise a breakpoint after
the IDT is installed. It should print a structured exception report and halt.

### Stage F — completed in v0.6: allocation and native interaction

Implemented:

- contiguous physical-page allocation
- a coalescing kernel heap with boot-time and interactive self-tests
- reusable external-interrupt registration
- native COM1 input and PS/2 Set-1 keyboard decoding
- a post-firmware shell rendered to both the framebuffer and serial terminal
- CI that interacts with the native shell after `ExitBootServices()`

Desktop PS/2 is relatively approachable. Modern laptops may expose the built-in
keyboard through an embedded controller, PS/2 compatibility, or USB/xHCI paths
depending on firmware/hardware. A robust modern-PC OS eventually needs USB
host-controller and HID work.

### Stage G — completed in v0.7: tooling and a bounded second kernel language

Implemented:

- a standard-library-only Python tool for FAT32 image creation and QEMU orchestration
- automatic OVMF discovery, serial marker timeouts, transcripts, and shell probes
- a fixed-size, versioned C module descriptor with documented ownership rules
- C-side descriptor validation and a deterministic cross-language boot self-test
- one statically linked `no_std` Rust FNV-1a module targeting Microsoft x64 COFF
- a `modules` shell command and CI markers proving the Rust code executed

### Stage H — next: hardening the foundations

Recommended order:

1. allocator fragmentation, exhaustion, invalid-free, and randomized invariant tests
2. an interrupt-routing harness followed by IRQ-driven PS/2 input
3. an IST-backed double-fault path and a kernel-stack guard page
4. Python-driven negative boot tests that require specific panic diagnostics

### Stage I — storage/files
Recommended order:

1. simple RAM filesystem
2. FAT32 reader
3. AHCI/SATA exploration
4. NVMe driver

### Stage J — processes
Implement:

- ring 3
- syscall/sysret
- context switching
- scheduler
- per-process address spaces

At that point ArgusOS is becoming a conventional kernel rather than a firmware monitor.

## Exercises

1. Add `cpuid 7` and print AVX2/SMEP/SMAP bits.
2. Add a `hexdump ADDRESS LENGTH` command, but restrict it to known RAM descriptors while learning.
3. Change `memmap` to aggregate bytes by UEFI memory type.
4. Add a command history ring.
5. Replace per-character fallback `OutputString` calls with buffered CHAR16 strings.
6. Replace the uppercase-only 5x7 glyph set with a complete 8x16 terminal font.
7. Add allocator tests for exhaustion, invalid frees, and fragmented maps.
8. Add an IST-backed double-fault handler and guard page for the kernel stack.
9. Add command history and cursor editing to the native kernel monitor.

## Important distinction

Call this version an **early UEFI-loaded kernel with a native post-boot
monitor**. The initial monitor is firmware-backed; the monitor reached through
`boot` owns its stack, page tables, heap, interrupt handling, console, and input,
and no longer uses Boot Services. It is a real kernel boundary, while still
being far from a general-purpose OS.
