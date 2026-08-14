# ArgusOS UEFI Study Kernel v0.11

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
- Runtime MMIO registration marks discovered device BAR mappings cache-disabled
  before drivers touch their registers.
- An Argus GDT/IDT handles all architectural CPU exceptions and dispatches
  registered external interrupt handlers.
- ACPI XSDT/MADT discovery locates processors and interrupt controllers.
- The legacy PIC is masked and a periodic local-APIC timer supplies interrupts.
- A native PS/2 Set-1 keyboard decoder handles normal keys, Shift, Caps Lock,
  Enter, Backspace, and Tab without firmware services.
- A post-firmware kernel monitor accepts commands through COM1 or PS/2 while
  rendering to the framebuffer and serial terminal.
- Versioned, validated C ABIs host statically linked `no_std` Rust checksum and
  RAMFS components without giving Rust boot, allocator, interrupt, or device ownership.
- A dependency-free Python host tool creates test media and drives interactive
  QEMU smoke tests through the native serial shell.
- Physical and heap allocators run fragmentation, exhaustion, invalid-free,
  double-free, and deterministic randomized invariant tests.
- The kernel stack begins with an unmapped 4 KiB guard page.
- A 64-bit TSS gives double faults a dedicated IST1 emergency stack.
- PS/2 input is delivered through an ACPI-aware I/O-APIC route into a bounded
  interrupt-safe queue, with polling retained as a hardware fallback.
- A bounded Rust RAM filesystem provides a first volatile file namespace with
  checked paths and create, read, enumerate, replace, and remove operations.
- A C-owned block-device ABI presents sector geometry and checked whole-sector reads.
- PCI configuration-space discovery locates AHCI-class SATA controllers.
- A polling, read-only AHCI driver performs IDENTIFY DEVICE and LBA48 DMA reads
  into PMM-owned memory after firmware exit.
- A bounded read-only Rust FAT32 parser validates BPB geometry, enumerates root
  8.3 entries, and follows FAT chains through the block-device boundary.

## What is *not* implemented yet?

The pre-boot monitor still uses UEFI keyboard input. The post-boot monitor owns
its input path, but USB keyboards are not supported yet; physical machines whose
firmware does not expose a PS/2-compatible keyboard will need an xHCI/USB HID
driver. Storage currently supports the first 512-byte-sector LBA48 SATA device
on the first discovered AHCI controller, using polling and a one-sector DMA
bounce buffer. There is no partition-table traversal, AHCI interrupt/NCQ path,
hotplug, storage write path, scheduler, userspace, networking, mouse/USB HID
stack, window system, or SMP startup.
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
post-firmware, allocator, paging, guard-page, TSS/IST, APIC-timer, and
kernel-shell markers. It drives COM1 commands, injects a QEMU hardware key
sequence through the PS/2 IRQ path, verifies allocator invariants, and performs a
RAMFS write/read/remove/not-found round trip. Separate negative boots require
breakpoint, stack-guard page-fault, and double-fault diagnostics. The same boot
also discovers QEMU's PCI AHCI controller, identifies and reads the SATA boot
disk through DMA, mounts that disk with the Rust FAT32 parser, and verifies root
listing, case-insensitive lookup, file reads, and missing-path handling. The
sparse in-memory FAT32 device remains available as a deterministic fallback.

The media creation, OVMF discovery, serial synchronization, and shell probes are
implemented by `tools/argus.py` instead of inline CI shell/Expect logic.

Useful host targets:

```sh
make host-check  # syntax-check the Python host tool
make test-image  # create build/argus-test.img
make smoke       # build, boot QEMU/OVMF, and probe the native shell
make fault-check # require breakpoint, guard-page, and double-fault diagnostics
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
bootguard
bootdouble
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
memtest
alloc 4096
modules
fs
ls
cat /README
write /notes Rust owns this file
rm /notes
pci
ahci
disks
fatinfo
fatls
fatcat /HELLO.TXT
ticks
input
irqtest
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
src/acpi.c      validated RSDP/XSDT/MADT discovery and IRQ overrides
src/arch.c      GDT, IDT, TSS/IST, exception dispatch, and PIC masking
src/apic.c      local APIC timer and I/O-APIC interrupt routing
src/pci.c       PCI configuration-space discovery and AHCI-class lookup
src/ahci.c      polling read-only SATA IDENTIFY/LBA48 DMA backend
src/block.h     versioned C-owned block-device descriptor and read contract
src/block.c     checked block layer and sparse FAT32 memory fixture
src/serial.c    direct 16550/COM1 output and nonblocking input
src/ps2.c       IRQ-driven PS/2 decoder, queue, and polling fallback
src/input.c     unified nonblocking COM1/PS2 input selection
src/kconsole.c  post-firmware framebuffer/serial console
src/kernel_shell.c native post-firmware command monitor
src/memory.c    freestanding memset/memcpy/memmove primitives
src/module_abi.h versioned cross-language descriptor and function contract
src/module.c     C-side module validation, registry, and boot self-test
src/rust_probe.rs bounded no_std checksum module behind ABI v1
src/ramfs_abi.h versioned C/Rust RAMFS descriptor and status contract
src/ramfs.c      C-owned RAMFS state, validation, tests, and shell-facing wrapper
src/rust_ramfs.rs bounded no_std RAMFS path and file-state implementation
src/fat32_abi.h versioned C/Rust read-only FAT32 parser contract
src/fat32.c      C-owned FAT32 state, wrapper, and cross-ABI self-tests
src/rust_fat32.rs bounded no_std BPB, directory, and FAT-chain parser
src/uefi_memory.c reusable memory-map allocation and validation
src/gop.c       GOP discovery and raw framebuffer pixel operations
src/console.c   Argus framebuffer terminal + UEFI text fallback
src/font5x7.c   tiny built-in 5x7 terminal font
src/cpu.S       CPU instructions, stack switch, and interrupt entry stubs
assets/HELLO.TXT hardware-backed FAT32 smoke-test payload
Makefile        freestanding PE/COFF build
tools/argus.py  FAT image creation and interactive QEMU smoke runner
docs/module-abi-v1.md ownership, layout, failure, and versioning contract
docs/ramfs-abi-v1.md RAMFS ownership, limits, paths, status, and validation contract
docs/block-device-abi-v1.md sector ownership, callback, and backend contract
docs/fat32-abi-v1.md FAT32 layout, ownership, bounds, and failure contract
docs/ahci-storage-v0.11.md PCI/AHCI ownership, DMA, limits, and recovery model
```

## Language policy

- C owns kernel integration, allocators, consoles, and early drivers.
- x86-64 assembly is limited to instructions and transitions C cannot express safely.
- Make coordinates the host build while Python owns stateful test orchestration.
- Python owns image construction and QEMU test orchestration on the host; it is
  not part of the boot image.
- Rust is restricted to bounded `no_std` components behind documented C ABIs.
  It currently owns a stateless checksum and the fixed-capacity RAMFS path/file
  state machine plus the read-only FAT32 parser, with no allocator or hardware access.
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

### Stage H — completed in v0.8: hardened foundations

Implemented:

- PMM bitmap/count validation plus fragmented-run and invalid/double-free probes
- heap structural validation, randomized allocation sequences, fragmentation,
  exhaustion, invalid-free, and double-free tests
- kernel-owned freestanding byte-copy/set primitives required by generated code
- a 4 KiB non-present guard below the 64 KiB kernel stack
- a 64-bit TSS and dedicated 16 KiB IST1 double-fault stack
- ACPI interrupt-override parsing and I/O-APIC routing for keyboard IRQ1
- an interrupt-produced PS/2 ring buffer with overflow accounting and polling fallback
- QEMU control-socket key injection that proves an IRQ-delivered shell command
- Python negative boots for breakpoint, guard-page, and true double-fault diagnostics

### Stage I — continued in v0.11: storage/files

Implemented:

- a fixed-capacity, allocator-free Rust RAMFS behind ABI v1
- absolute-path validation rejecting empty, repeated, `.` and `..` segments
- create/replace, read, enumeration, removal, and slot-reuse behavior
- C-owned aligned opaque state with no ownership transfer across the ABI
- `fs`, `ls`, `cat`, `write`, and `rm` native-shell commands
- boot-time exhaustion/bounds/path tests and a QEMU command-level round trip
- a versioned C block-device interface with checked LBA, count, and buffer bounds
- a sparse standards-shaped FAT32 fixture with 65,525 data clusters
- a bounded read-only Rust FAT32 parser behind its own ABI v1
- BPB, FAT-capacity, cluster, chain-cycle, path, and output-bound validation
- `disks`, `fatinfo`, `fatls`, and `fatcat` native-shell commands
- boot and QEMU tests that enumerate and read `/HELLO.TXT`
- legacy PCI configuration-space discovery with AHCI class matching
- PCI memory-space and bus-master enablement without clearing status bits
- AHCI BIOS/OS ownership handoff, port discovery, engine reconfiguration, and
  PMM-owned command-list/FIS/table/data memory
- read-only ATA IDENTIFY DEVICE and one-sector LBA48 DMA reads
- QEMU q35/SATA boot-media tests that prove Rust is parsing the hardware-backed disk
- `pci` and `ahci` native-shell diagnostics

Recommended next order:

1. protective-MBR/GPT partition discovery and filesystem block slices
2. FAT32 subdirectory and long-filename support through ABI v2
3. AHCI interrupt completion, multi-sector PRDTs, recovery, and NCQ exploration
4. NVMe read-only block backend
5. a write/cache layer only after power-loss and corruption semantics are designed

### Stage J — processes
Implement:

- ring 3
- syscall/sysret
- context switching
- scheduler
- per-process address spaces

At that point ArgusOS is becoming a conventional kernel rather than a firmware monitor.

### Stage K — input and UI groundwork

Implement:

- xHCI discovery and USB HID keyboard/mouse input
- mouse events, clipping, compositing surfaces, and a framebuffer blitter
- bitmap/font asset loading through the read-only filesystem
- a minimal user-space display-server protocol

This is where UI construction starts, but it remains a graphics/input foundation
rather than a desktop.

### Stage L — C++ windowed UI

C++ enters the build here, after userspace, syscalls, scheduling, input, and file
loading exist. It will own the user-space window server, retained widget tree,
layout, controls, and applications. Kernel drivers and ownership boundaries stay
in C; format parsers and other bounded data components may remain Rust. The first
visible milestone is a mouse-driven desktop with one terminal window.

## Exercises

1. Add `cpuid 7` and print AVX2/SMEP/SMAP bits.
2. Add a `hexdump ADDRESS LENGTH` command, but restrict it to known RAM descriptors while learning.
3. Change `memmap` to aggregate bytes by UEFI memory type.
4. Add a command history ring.
5. Replace per-character fallback `OutputString` calls with buffered CHAR16 strings.
6. Replace the uppercase-only 5x7 glyph set with a complete 8x16 terminal font.
7. Add command history and cursor editing to the native kernel monitor.
8. Add directory-aware RAMFS enumeration without changing ABI v1.
9. Extend the FAT32 reader to one bounded subdirectory level through ABI v2.

## Important distinction

Call this version an **early UEFI-loaded kernel with a native post-boot
monitor**. The initial monitor is firmware-backed; the monitor reached through
`boot` owns its stack, page tables, heap, interrupt handling, console, and input,
and no longer uses Boot Services. Guard pages and an IST-backed double-fault path
contain early stack failures, while a volatile Rust RAMFS supplies the first file
namespace and a read-only Rust parser consumes sectors from the first block-device
contract. It is a real kernel boundary, while still being far from a
general-purpose OS or graphical desktop.
