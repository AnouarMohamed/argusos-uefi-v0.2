CLANG ?= clang
LLD   ?= lld-link
CFLAGS = -target x86_64-pc-win32-coff -ffreestanding -fshort-wchar -mno-red-zone \
         -fno-stack-protector -fno-builtin -Wall -Wextra -O2

OBJS = build/main.obj build/cpu.obj build/gop.obj build/console.obj build/font5x7.obj

all: build/BOOTX64.EFI

build:
	mkdir -p build

build/main.obj: src/main.c src/efi.h src/console.h src/gop.h | build
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
