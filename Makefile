CLANG ?= clang
LLD   ?= lld-link
CFLAGS = -target x86_64-pc-win32-coff -ffreestanding -fshort-wchar -mno-red-zone \
         -fno-stack-protector -fno-builtin -Wall -Wextra -O2

all: build/BOOTX64.EFI

build:
	mkdir -p build

build/main.obj: src/main.c src/efi.h | build
	$(CLANG) $(CFLAGS) -c $< -o $@

build/cpu.obj: src/cpu.S | build
	$(CLANG) -target x86_64-pc-win32-coff -c $< -o $@

build/BOOTX64.EFI: build/main.obj build/cpu.obj
	$(LLD) /subsystem:efi_application /entry:efi_main /nodefaultlib \
	       /out:$@ $^

usb-tree: build/BOOTX64.EFI
	mkdir -p EFI/BOOT
	cp build/BOOTX64.EFI EFI/BOOT/BOOTX64.EFI

clean:
	rm -rf build EFI/BOOT/BOOTX64.EFI

.PHONY: all clean usb-tree
