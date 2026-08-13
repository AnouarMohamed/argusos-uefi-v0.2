# ArgusOS UEFI Study Monitor v0.2

A deliberately small x86-64 UEFI bare-metal study environment.

This is the practical successor to the 16-bit BIOS ArgusOS shell. It boots on modern UEFI firmware as `EFI/BOOT/BOOTX64.EFI`, does not use libc or a host operating system, and keeps UEFI Boot Services active so keyboard and console input remain reliable on modern machines while you study the lower layers.

## What is real here?

- The firmware loads your PE/COFF executable directly.
- There is no Windows/Linux/DOS underneath it.
- `cpu_vendor`, `cpu_cpuid1`, and `cpu_read_tsc` are handwritten x86-64 assembly.
- `mem`/`memmap` inspect the actual UEFI physical memory map.
- `reboot` and `shutdown` invoke firmware runtime services.
- The shell, parsing, integer formatting, and console logic have no libc dependency.

## What is *not* a full kernel yet?

This stage intentionally has **not called `ExitBootServices()`**. It still relies on UEFI for keyboard and text output. That is a feature for the learning progression: if you exit firmware services immediately on a modern laptop, you need to supply your own framebuffer terminal and keyboard stack (often USB HID/xHCI), which is a much larger project.

## Build

Requires Clang 17-ish and `lld-link`:

```sh
make
```

Output:

```text
build/BOOTX64.EFI
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
color 14
echo hello
reboot
shutdown
exit
```

## Source map

```text
src/efi.h     minimal UEFI ABI/types/protocol structures
src/main.c    shell + console + UEFI memory/time/runtime interaction
src/cpu.S     handwritten x86-64 CPUID and RDTSC routines
Makefile      freestanding PE/COFF build
```

## Study progression

### Stage A — current version
Learn:

- Microsoft x64 / UEFI calling convention
- PE/COFF EFI entry points
- CPUID and RDTSC
- firmware memory descriptors
- freestanding C/assembly interoperability

### Stage B — own framebuffer terminal
Use UEFI GOP only during startup, obtain the framebuffer address, then implement:

- pixel writes
- bitmap font rendering
- cursor
- scrolling
- `printf`-like formatting you write yourself

At this point screen output no longer needs `ConOut`.

### Stage C — memory ownership
Implement:

- parse memory map
- page-frame bitmap allocator
- early bump allocator
- kernel heap

### Stage D — leave firmware boot services
Correct sequence:

1. obtain final memory map
2. get its map key
3. call `ExitBootServices(image, map_key)`
4. never call UEFI Boot Services again

Runtime Services are a separate topic.

### Stage E — interrupts
Implement:

- GDT
- IDT
- exception handlers
- APIC/PIC exploration
- timer interrupt

### Stage F — keyboard
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
5. Replace per-character `OutputString` calls with buffered CHAR16 strings.
6. Use GOP and draw your own terminal without `ConOut`.
7. Write a physical-page allocator and show free page counts.
8. Add an assembly exception stub for divide-by-zero after you have an IDT.

## Important distinction

Call this version a **UEFI bare-metal monitor / pre-kernel environment**. It is more modern and more useful for study than the old BIOS shell, but it is intentionally not pretending that retaining firmware Boot Services is the same thing as a self-sufficient kernel.
