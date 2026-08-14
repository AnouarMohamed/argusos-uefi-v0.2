# ArgusOS PCI/AHCI storage v0.11

ArgusOS v0.11 replaces the test-only storage path during supported boots with a
real, read-only SATA block backend. The implementation is intentionally small:
PCI and AHCI device ownership remain in C, while the existing bounded Rust FAT32
module continues to see only the versioned block-device ABI.

## Discovery and ownership

`src/pci.c` scans PCI configuration mechanism 1 through ports `0xCF8` and
`0xCFC`. It records functions and selects class `01`, subclass `06`, programming
interface `01`. Enabling the controller uses a 16-bit command-register write so
read status bits are not accidentally written back as write-one-to-clear bits.

`src/ahci.c` validates BAR5 against the active identity-map limit, requests BIOS
ownership when the controller advertises the handoff capability, disables HBA
interrupt delivery, and selects the first implemented port with an active SATA
signature. Before the first register access, `paging_mark_mmio` adds cache-disable
and write-through attributes to every identity mapping overlapping the ABAR and
reloads CR3 so the new memory type is active.

## DMA layout

The PMM permanently lends the driver two contiguous 4 KiB pages:

| Location | Use |
|---|---|
| page 0, bytes 0–1023 | 32-entry AHCI command list |
| page 0, bytes 1024–1279 | received-FIS area |
| page 0, bytes 1280–1535 | slot-zero command table and one PRDT entry |
| page 1 | 512-byte DMA bounce buffer |

All programmed addresses are physical addresses and are also reachable through
the current identity map. Controllers without 64-bit DMA support are accepted
only when both pages fit below 4 GiB.

## Command model

The driver uses slot zero, polling, and one PRDT entry. It issues ATA IDENTIFY
DEVICE during initialization, requires LBA48 and 512-byte logical sectors, then
implements the block callback with one READ DMA EXT command per requested sector.
Callers may request at most 128 sectors per callback; data is copied from the
fixed DMA page one sector at a time.

Every command has bounded waits for task-file readiness and command completion.
A task-file error or timeout takes the device offline and stops its command
engine before returning an I/O failure. No DMA memory is released while a read
could still reference it.

## Current limits

- first AHCI controller and first active SATA disk only;
- raw FAT32 volume beginning at disk LBA 0;
- 512-byte logical sectors and LBA48;
- polling slot-zero commands with no interrupts, NCQ, hotplug, or power management;
- read-only operation with no flush, cache, or recovery reset sequence;
- no MBR, GPT, or filesystem partition slices yet.

QEMU smoke and fault boots use the q35 chipset and attach the generated FAT32
image to its ICH9 AHCI controller. The image contains `/HELLO.TXT`; boot markers
and shell probes require PCI discovery, IDENTIFY, `ahci0` selection, Rust FAT32
enumeration, and the expected file bytes.
