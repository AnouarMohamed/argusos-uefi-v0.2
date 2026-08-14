# ArgusOS block-device ABI v1

The block-device ABI separates filesystem parsers from storage hardware. Its C
layout in [`src/block.h`](../src/block.h) is the source of truth.

## Contract

`argus_block_device_v1_t` is a 64-byte, C-owned descriptor:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `abi_version` | Must be `ARGUS_BLOCK_ABI_VERSION` (`1`). |
| 4 | 4 | `struct_size` | Must be 64. |
| 8 | 24 | `name` | NUL-terminated printable device name. |
| 32 | 4 | `sector_size` | Power-of-two logical-sector bytes, at least 512. |
| 36 | 4 | `reserved` | Must be zero. |
| 40 | 8 | `sector_count` | Number of addressable logical sectors. |
| 48 | 8 | `context` | Opaque backend-owned context pointer. |
| 56 | 8 | `read` | Microsoft x64 C read callback. |

The callback reads whole sectors into a caller-owned buffer. Reads with a zero
count, an out-of-range LBA span, integer overflow, a null output, or insufficient
capacity fail before the backend executes. The descriptor and context remain
owned by C; a filesystem may borrow them only for the duration documented by its
own ABI.

## Current backend

ArgusOS v0.10 registers `memory.fat32`, a sparse in-memory fixture. It reports
66,069 sectors while synthesizing only the sectors a valid minimal FAT32 volume
needs: primary and backup boot sectors, FSInfo, the first FAT sector, one root
directory sector, and one data sector. Unspecified sectors read as zero.

The fixture deliberately crosses the FAT32 minimum of 65,525 data clusters. It
therefore tests a standards-shaped FAT32 geometry without embedding roughly
32 MiB of static bytes in the EFI image.

Future AHCI, NVMe, USB-mass-storage, and RAM-disk implementations should publish
the same descriptor. Filesystem code must not depend on backend-specific context.

ABI v1 is frozen; incompatible layout or callback changes require a new type and
version.
