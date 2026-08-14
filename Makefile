CLANG ?= clang
LLD   ?= lld-link
RUSTC ?= rustc
PYTHON ?= python3
CFLAGS = -target x86_64-pc-win32-coff -ffreestanding -fshort-wchar -mno-red-zone \
         -fno-stack-protector -fno-builtin -Wall -Wextra -O2
RUST_TARGET = x86_64-pc-windows-msvc
RUSTFLAGS = --target $(RUST_TARGET) --crate-type lib --emit=obj \
            --edition=2021 -D warnings -C panic=abort \
            -C opt-level=2 -C overflow-checks=yes

OBJS = build/main.obj build/boot.obj build/kernel.obj build/acpi.obj build/ahci.obj build/apic.obj build/block.obj \
       build/arch.obj build/desktop.obj build/heap.obj build/input.obj build/kconsole.obj \
       build/kernel_shell.obj build/memory.obj build/module.obj build/paging.obj build/fat32.obj \
       build/pci.obj build/pmm.obj build/ps2.obj build/serial.obj \
       build/ramfs.obj build/rust_probe.obj build/rust_ramfs.obj build/rust_fat32.obj \
       build/uefi_memory.obj build/cpu.obj build/gop.obj build/console.obj build/font5x7.obj

all: build/BOOTX64.EFI

build:
	mkdir -p build

build/main.obj: src/main.c src/efi.h src/boot.h src/console.h src/gop.h src/serial.h src/uefi_memory.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/boot.obj: src/boot.c src/boot.h src/boot_info.h src/efi.h src/console.h src/gop.h src/kernel.h src/serial.h src/uefi_memory.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kernel.obj: src/kernel.c src/kernel.h src/acpi.h src/ahci.h src/apic.h src/arch.h src/block.h src/boot_info.h src/fat32.h src/fat32_abi.h src/heap.h src/kconsole.h src/kernel_shell.h src/module.h src/module_abi.h src/paging.h src/pci.h src/pmm.h src/ramfs.h src/ramfs_abi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/acpi.obj: src/acpi.c src/acpi.h src/boot_info.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/ahci.obj: src/ahci.c src/ahci.h src/block.h src/paging.h src/pci.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/apic.obj: src/apic.c src/apic.h src/acpi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/block.obj: src/block.c src/block.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/desktop.obj: src/desktop.c src/desktop.h src/console.h src/font5x7.h src/gop.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/arch.obj: src/arch.c src/arch.h src/apic.h src/kernel.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/heap.obj: src/heap.c src/heap.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/input.obj: src/input.c src/input.h src/acpi.h src/ps2.h src/serial.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kconsole.obj: src/kconsole.c src/kconsole.h src/console.h src/serial.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/kernel_shell.obj: src/kernel_shell.c src/kernel_shell.h src/acpi.h src/ahci.h src/apic.h src/block.h src/boot_info.h src/desktop.h src/fat32.h src/fat32_abi.h src/heap.h src/input.h src/kconsole.h src/module.h src/module_abi.h src/paging.h src/pci.h src/pmm.h src/ramfs.h src/ramfs_abi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/memory.obj: src/memory.c | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/module.obj: src/module.c src/module.h src/module_abi.h src/fat32_abi.h src/ramfs_abi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/rust_probe.obj: src/rust_probe.rs rust-toolchain.toml | build
	$(RUSTC) $(RUSTFLAGS) --crate-name argus_rust_probe $< -o $@

build/ramfs.obj: src/ramfs.c src/ramfs.h src/ramfs_abi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/rust_ramfs.obj: src/rust_ramfs.rs rust-toolchain.toml | build
	$(RUSTC) $(RUSTFLAGS) --crate-name argus_rust_ramfs $< -o $@

build/fat32.obj: src/fat32.c src/fat32.h src/fat32_abi.h src/block.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/rust_fat32.obj: src/rust_fat32.rs rust-toolchain.toml | build
	$(RUSTC) $(RUSTFLAGS) --crate-name argus_rust_fat32 $< -o $@

build/paging.obj: src/paging.c src/paging.h src/acpi.h src/boot_info.h src/pmm.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/pci.obj: src/pci.c src/pci.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/pmm.obj: src/pmm.c src/pmm.h src/boot_info.h src/uefi_memory.h src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/ps2.obj: src/ps2.c src/ps2.h src/acpi.h src/apic.h src/arch.h | build
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

test-image: build/BOOTX64.EFI
	$(PYTHON) tools/argus.py image --efi $< --output build/argus-test.img

smoke: build/BOOTX64.EFI
	$(PYTHON) tools/argus.py smoke --efi $<

fault-check: build/BOOTX64.EFI
	$(PYTHON) tools/argus.py fault --efi $< --case breakpoint --log build/breakpoint.log
	$(PYTHON) tools/argus.py fault --efi $< --case guard --log build/guard.log
	$(PYTHON) tools/argus.py fault --efi $< --case double --log build/double.log

host-check: | build
	PYTHONPYCACHEPREFIX=build/pycache $(PYTHON) -m py_compile tools/argus.py

clean:
	rm -rf build EFI/BOOT/BOOTX64.EFI

.PHONY: all clean fault-check host-check smoke test-image usb-tree
