# ArgusOS UEFI Study Kernel v0.16

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
  Enter, Backspace, Tab, and extended arrow keys without firmware services.
- A post-firmware kernel monitor accepts commands through COM1 or PS/2 while
  rendering to the framebuffer and serial terminal.
- A restrained ArgusOS desktop composites retained Console, System, Files, and Snake
  surfaces with focus, z-order, damage tracking, dragging, and a software pointer.
- Versioned, validated C ABIs host statically linked `no_std` Rust checksum and
  RAMFS components without giving Rust boot, allocator, interrupt, or device ownership.
- A dependency-free Python host tool creates test media and drives interactive
  QEMU smoke tests through the native serial shell.
- Physical and heap allocators run fragmentation, exhaustion, invalid-free,
  double-free, and deterministic randomized invariant tests.
- The kernel stack begins with an unmapped 4 KiB guard page.
- A 64-bit TSS gives double faults a dedicated IST1 emergency stack.
- PS/2 keyboard and three-button mouse input use ACPI-aware I/O-APIC routes and
  bounded interrupt-safe queues, with polling retained as a hardware fallback.
- A bounded Rust RAM filesystem provides a first volatile file namespace with
  checked paths and create, read, enumerate, replace, and remove operations.
- A C-owned block-device ABI presents sector geometry and checked whole-sector reads.
- PCI configuration-space discovery locates AHCI-class SATA controllers.
- A polling, read-only AHCI driver performs IDENTIFY DEVICE and LBA48 DMA reads
  into PMM-owned memory after firmware exit.
- A bounded read-only Rust FAT32 parser validates BPB geometry, enumerates root
  8.3 entries, and follows FAT chains through the block-device boundary.
- Two boot probe processes execute at CPU privilege level 3 with private CR3
  roots, separate code/stack pages, an unmapped stack guard, and W^X mappings.
- A versioned syscall ABI provides bounded serial writes, PID lookup, cooperative
  yield, and exit through x86-64 `SYSCALL`/`SYSRET` transitions.
- A minimal round-robin scheduler preserves user register state across yields and
  proves that both isolated probe processes resume and exit successfully.
- A persistent freestanding C++20 Snake process runs at ring 3 with no standard
  library, exceptions, RTTI, writable globals, allocator, or direct hardware access.
- The kernel validates fixed-size Snake frames, routes focused PS/2 arrow/WASD
  input, and renders the game through the existing retained compositor.

## What is *not* implemented yet?

The pre-boot monitor accepts UEFI keyboard or direct COM1 input. The post-boot
monitor owns its input path, but USB keyboards are not supported yet; physical
machines whose firmware does not expose a PS/2-compatible keyboard will need an
xHCI/USB HID driver. Storage currently supports the first 512-byte-sector LBA48
SATA device on the first discovered AHCI controller, using polling and a one-sector
DMA bounce buffer. There is no partition-table traversal, AHCI interrupt/NCQ path,
hotplug, storage write path, preemptive scheduler, ELF loader, dynamic process
creation, user-mode fault recovery, IPC, networking, USB HID stack, user-space
display server, or SMP startup. The v0.14 compositor and utility apps remain
kernel-hosted prototypes, not process-isolated applications. Snake is a persistent
v0.16 user process, but its flat image is embedded at build time and its narrow
frame ABI is game-specific rather than a general display protocol.
Runtime Services memory is preserved, but the kernel does not call Runtime
Services after the handoff.

## Build

Requires Python 3.10+, Clang/Clang++ 17-ish, `lld-link`, `ld.lld`, GNU `objcopy`,
and Rust 1.97.1 with the
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
post-firmware, allocator, paging, guard-page, TSS/IST, APIC-timer, ring-3,
syscall, address-space, scheduler, and kernel-shell markers. It drives COM1 commands,
injects a QEMU hardware key
sequence through the PS/2 IRQ path, verifies allocator invariants, and performs a
RAMFS write/read/remove/not-found round trip. Separate negative boots require
breakpoint, stack-guard page-fault, and double-fault diagnostics. The same boot
also discovers QEMU's PCI AHCI controller, identifies and reads the SATA boot
disk through DMA, mounts that disk with the Rust FAT32 parser, and verifies root
listing, case-insensitive lookup, file reads, and missing-path handling. It now
boots with a standard VGA device, requires keyboard and mouse IRQ markers, drags
the console through damage-aware composition, focuses the System app, and captures
the initial, moved, and focus states. It also focuses Snake, injects a hardware
direction key, requires a new ring-3 frame, and captures the playable game window.
The defining palette colors are verified in every framebuffer capture.
The sparse in-memory FAT32 device remains available as a deterministic fallback.

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
processes
snake
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
apps
focus system
ui
desktop
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
src/process.c   isolated user images, syscall validation, and cooperative scheduler
src/process.h   process state and kernel-facing diagnostics
src/user_abi.h  versioned syscall numbers shared with freestanding user programs
src/snake_abi.h validated C/C++ game-frame contract
src/input_keys.h shared extended keyboard values
src/acpi.c      validated RSDP/XSDT/MADT discovery and IRQ overrides
src/arch.c      GDT, IDT, TSS/IST, exception dispatch, and PIC masking
src/apic.c      local APIC timer and I/O-APIC interrupt routing
src/pci.c       PCI configuration-space discovery and AHCI-class lookup
src/ahci.c      polling read-only SATA IDENTIFY/LBA48 DMA backend
src/block.h     versioned C-owned block-device descriptor and read contract
src/block.c     checked block layer and sparse FAT32 memory fixture
src/serial.c    direct 16550/COM1 output and nonblocking input
src/ps2.c       IRQ-driven keyboard/mouse decoding, queues, and polling fallback
src/input.c     unified nonblocking COM1/PS2 input selection
src/kconsole.c  post-firmware framebuffer/serial console
src/kernel_shell.c native post-firmware command monitor
src/surface.c   heap-backed retained pixel surfaces, damage bounds, text, and scroll
src/compositor.c clipped damage composition, window positions, z-order, and hit tests
src/desktop.c   desktop policy, pointer, task switching, and built-in utility apps
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
src/user_images.S embeds separately linked user images in the EFI payload
user/snake.cpp  freestanding persistent C++20 Snake process
user/snake.ld   fixed-address user image layout and size/storage assertions
assets/HELLO.TXT hardware-backed FAT32 smoke-test payload
Makefile        freestanding PE/COFF build
tools/argus.py  FAT image creation and interactive QEMU smoke runner
docs/module-abi-v1.md ownership, layout, failure, and versioning contract
docs/ramfs-abi-v1.md RAMFS ownership, limits, paths, status, and validation contract
docs/block-device-abi-v1.md sector ownership, callback, and backend contract
docs/fat32-abi-v1.md FAT32 layout, ownership, bounds, and failure contract
docs/ahci-storage-v0.11.md PCI/AHCI ownership, DMA, limits, and recovery model
docs/desktop-ui-v0.12.md first desktop slice, renderer boundary, and limitations
docs/pointer-ui-v0.13.md PS/2 mouse, cursor, hit testing, and dragging boundary
docs/surface-compositor-v0.14.md retained surfaces, compositor, and app boundary
docs/userspace-v0.15.md ring-3, syscall, address-space, and scheduler boundary
docs/cpp-snake-v0.16.md C++ image, game ABI, input, rendering, and limits
PRODUCT.md      product intent, personality, anti-references, and principles
DESIGN.md       normative ArgusOS desktop tokens and component rules
```

## Language policy

- C owns kernel integration, allocators, consoles, early drivers, and the current
  kernel-hosted compositor because those paths directly control memory and hardware.
- x86-64 assembly is limited to instructions and transitions C cannot express safely.
- Make coordinates the host build while Python owns stateful test orchestration.
- Python owns image construction and QEMU test orchestration on the host; it is
  not part of the boot image.
- Rust is restricted to bounded `no_std` components behind documented C ABIs.
  It currently owns a stateless checksum and the fixed-capacity RAMFS path/file
  state machine plus the read-only FAT32 parser used by the Files app, with no
  allocator or hardware access.
- C++ owns the persistent ring-3 Snake game logic and is reserved for future user
  applications and the display-server toolkit. It is freestanding and exception-free;
  its first flat image remains statically embedded until executable loading exists.
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

### Stage J — first process boundary, completed in v0.15

Implemented:

- ring-3 entry through an architectural IRET frame
- `SYSCALL`/`SYSRET` with a dedicated kernel syscall stack
- complete integer and Microsoft x64 nonvolatile context preservation
- private page-table roots and distinct physical code/stack pages
- read-only executable code, writable NX stacks, and unmapped stack guards
- a bounded syscall ABI for write, getpid, yield, and exit
- cooperative round-robin scheduling of two independent user probes
- boot and shell tests for isolation, scheduling, and successful completion

Still implement:

- general process lifecycle beyond the persistent Snake app and boot-time probes
- preemption and timer-driven scheduling
- ELF loading and dynamic process creation
- user exception recovery, termination, IPC, and shared display surfaces

This is the first conventional kernel/userspace boundary, but not yet a general
application runtime.

### Stage K — continued in v0.14: retained surfaces and utility apps

Implemented:

- a restrained ArgusOS desktop with square one-pixel chrome and no decorative copy
- a kernel-mode desktop and functional bottom task switcher
- clipped rectangular framebuffer scrolling for terminal viewports
- a region-aware framebuffer console whose `clear` operation preserves chrome
- a `desktop` shell command for deterministic redraws
- PS/2 auxiliary-port setup, three-byte packet decoding, and an IRQ12 event queue
- a background-preserving software pointer with button state
- title-bar hit testing and pixel-preserving drag movement
- heap-backed retained pixel surfaces with clipped drawing and scroll operations
- a damage-aware compositor with z-order, focus, hit testing, and bounded damage lists
- Console, System, and Files surfaces backed by real kernel and filesystem state
- periodic System updates and Files refresh after RAMFS mutation
- graphical QEMU smoke coverage for dragging, damage, focus, and framebuffer captures

Still implement:

- xHCI discovery and USB HID keyboard/mouse input
- bitmap/font asset loading through the read-only filesystem
- a minimal user-space display-server protocol

The v0.14 desktop proves the surface contract and interaction model. Its apps are
intentionally small and honest, but still execute inside the kernel. v0.16 adds
Snake as a separate user process without relabeling those utilities.

### Stage L: first C++ user app, completed in v0.16

Implemented:

- a separately linked freestanding C++20 image with a fixed 16 KiB code budget
- build-time rejection of writable globals and unresolved runtime dependencies
- a persistent cooperative ring-3 process with its own CR3 and guarded stack
- clock, focused-input, and validated Snake-frame syscalls
- Nokia-style block Snake mechanics with score, food, collision, and restart
- PS/2 arrow keys plus WASD, with serial control preserved for diagnostics
- a muted monochrome Snake surface in the existing focus, task, drag, and damage model
- QEMU gameplay coverage and a dedicated framebuffer capture

Still implement:

- a generic IPC/shared-surface protocol instead of the game-specific frame call
- process creation from ELF files rather than embedded flat images
- user-space ownership of the compositor and application toolkit
- preemption, blocking waits, and user-fault termination

Kernel drivers and framebuffer ownership stay in C for now. Rust continues to own
bounded data components. The next architectural step is a general user application
channel that can support a terminal client without adding one syscall per app.

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
general-purpose OS. Its graphical desktop now has retained surfaces, three
kernel-hosted utility apps, one persistent C++ ring-3 game, focus, z-order, and
damage-aware movement, but it is not yet a general multi-process GUI.
