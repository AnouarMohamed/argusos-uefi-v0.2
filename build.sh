#!/usr/bin/env sh
set -eu
make clean
make
make usb-tree
printf '\nBuilt: build/BOOTX64.EFI\nUSB path: EFI/BOOT/BOOTX64.EFI\n'
