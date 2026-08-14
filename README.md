# ArgusOS UEFI Study Kernel v0.5

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
- A bitmap physical-page allocator owns conventional RAM and protects permanent reservations.
- The allocator runs an allocate/free self-test on every kernel entry.
- COM1 output remains available after firmware services are gone.
- Kernel-owned page tables enable write protection and NX where possible.
- An Argus GDT/IDT handles all architectural CPU exceptions.
- ACPI XSDT/MADT discovery locates processors and interrupt controllers.
- The legacy PIC is masked and a periodic local-APIC timer supplies interrupts.

## What is *not* implemented yet?

The pre-boot monitor still uses UEFI keyboard input. After `boot`, the kernel can
write to the framebuffer and COM1 but intentionally idles after proving its APIC
timer because it does not yet have a native keyboard driver, scheduler,
filesystem, or userspace.
Runtime Services memory is preserved, but the kernel does not call Runtime
Services after the handoff.

## Build

Requires Clang 17-ish and `lld-link`:

```sh
make
```

Output:

```text
build/BOOTX64.EFI
```

CI performs the same freestanding build with warnings treated as errors, creates
a FAT32 boot image, boots it with QEMU/OVMF, enters `boot`, and requires explicit
post-firmware, allocator, paging, IDT, and APIC-timer markers.

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

## Commands

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

## Source map

```text
src/efi.h       minimal UEFI ABI/types/protocol structures
src/main.c      pre-boot monitor + UEFI time/runtime interaction
src/boot.c      final memory-map capture + ExitBootServices handoff
src/boot_info.h immutable firmware-to-kernel handoff contract
src/kernel.c    post-firmware kernel entry and self-tests
src/pmm.c       tracked physical-page bitmap allocator
src/paging.c    kernel-owned identity page tables and memory attributes
src/acpi.c      validated RSDP/XSDT/MADT discovery
src/arch.c      GDT, IDT, exception dispatch, and legacy PIC masking
src/apic.c      xAPIC/x2APIC activation and periodic local timer
src/serial.c    direct 16550/COM1 output
src/uefi_memory.c reusable memory-map allocation and validation
src/gop.c       GOP discovery and raw framebuffer pixel operations
src/console.c   Argus framebuffer terminal + UEFI text fallback
src/font5x7.c   tiny built-in 5x7 terminal font
src/cpu.S       CPU instructions, stack switch, and interrupt entry stubs
Makefile        freestanding PE/COFF build
```

## Language policy

- C owns the kernel, allocators, parsers, consoles, and early drivers.
- x86-64 assembly is limited to instructions and transitions C cannot express safely.
- Make and shell own the host build and test workflow.
- A richer host language can be added for tooling when the tooling justifies it.
- A second kernel language should wait until the heap, interrupt policy, and kernel ABI are stable.

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

### Stage F — next: native input and a post-boot monitor
Desktop PS/2 is relatively approachable. Modern laptops may expose the built-in keyboard through an embedded controller, PS/2 compatibility, or USB/xHCI paths depending on firmware/hardware. A robust modern-PC OS eventually needs USB host-controller and HID work.

### Stage G — storage/files
Recommended order:

1. simple RAM filesystem
2. FAT32 reader
3. AHCI/SATA exploration
4. NVMe driver

### Stage H — processes
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

## Important distinction

Call this version an **early UEFI-loaded kernel with a pre-boot monitor**. The
interactive monitor is firmware-backed, but the code reached through `boot` owns
its stack and conventional physical memory and no longer uses Boot Services. It
is a real kernel boundary, while still being far from a general-purpose OS.
