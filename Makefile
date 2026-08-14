CLANG ?= clang
LLD   ?= lld-link
CFLAGS = -target x86_64-pc-win32-coff -ffreestanding -fshort-wchar -mno-red-zone \
         -fno-stack-protector -fno-builtin -Wall -Wextra -O2

OBJS = build/main.obj build/boot.obj build/kernel.obj build/acpi.obj build/apic.obj \
       build/arch.obj build/heap.obj build/input.obj build/kconsole.obj \
       build/kernel_shell.obj build/paging.obj build/pmm.obj build/ps2.obj build/serial.obj \
       build/uefi_memory.obj build/cpu.obj build/gop.obj build/console.obj build/font5x7.obj

all: build/BOOTX64.EFI

build:
	mkdir -p build

build/main.obj: src/main.c src/efi.h src/boot.h src/console.h src/gop.h src/uefi_memory.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/boot.obj: src/boot.c src/boot.h src/boot_info.h src/efi.h src/console.h src/gop.h src/kernel.h src/serial.h src/uefi_memory.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kernel.obj: src/kernel.c src/kernel.h src/acpi.h src/apic.h src/arch.h src/boot_info.h src/heap.h src/kconsole.h src/kernel_shell.h src/paging.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/acpi.obj: src/acpi.c src/acpi.h src/boot_info.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/apic.obj: src/apic.c src/apic.h src/acpi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/arch.obj: src/arch.c src/arch.h src/apic.h src/kernel.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/heap.obj: src/heap.c src/heap.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/input.obj: src/input.c src/input.h src/ps2.h src/serial.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kconsole.obj: src/kconsole.c src/kconsole.h src/console.h src/serial.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kernel_shell.obj: src/kernel_shell.c src/kernel_shell.h src/acpi.h src/apic.h src/boot_info.h src/heap.h src/input.h src/kconsole.h src/paging.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/paging.obj: src/paging.c src/paging.h src/acpi.h src/boot_info.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/pmm.obj: src/pmm.c src/pmm.h src/boot_info.h src/uefi_memory.h src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/ps2.obj: src/ps2.c src/ps2.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/serial.obj: src/serial.c src/serial.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/uefi_memory.obj: src/uefi_memory.c src/uefi_memory.h src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/cpu.obj: src/cpu.S | build
	$(CLANG) -target x86_64-pc-win32-coff -c $< -o $@

build/gop.obj: src/gop.c src/gop.h src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/console.obj: src/console.c src/console.h src/gop.h src/font5x7.h src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/font5x7.obj: src/font5x7.c src/font5x7.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/BOOTX64.EFI: $(OBJS)
	$(LLD) /subsystem:efi_application /entry:efi_main /nodefaultlib \
	       /out:$@ $(OBJS)

usb-tree: build/BOOTX64.EFI
	mkdir -p EFI/BOOT
	cp build/BOOTX64.EFI EFI/BOOT/BOOTX64.EFI

clean:
	rm -rf build EFI/BOOT/BOOTX64.EFI

.PHONY: all clean usb-tree
