# ArgusOS FAT32 reader ABI v1

This document defines the bounded C/Rust interface for the read-only FAT32
reader introduced in ArgusOS v0.10. The C layout in
[`src/fat32_abi.h`](../src/fat32_abi.h) is authoritative. The `no_std` Rust
implementation is [`src/rust_fat32.rs`](../src/rust_fat32.rs).

## Scope

FAT32 reader v1 mounts a C-owned block device, reports volume geometry,
enumerates short 8.3 entries in the root directory, and reads root files through
their FAT chains. Lookup is ASCII case-insensitive.

The initial ABI is intentionally bounded:

- 512-byte logical sectors;
- one or two FATs;
- power-of-two clusters from 1 through 128 sectors;
- 48 path bytes;
- 4,096 output bytes per read;
- at most 4,096 root-directory clusters per scan;
- short 8.3 names in the root directory only;
- no writes, long filenames, nested traversal, allocation, or hardware access.

## Ownership

C owns the block descriptor and aligned opaque parser state. Rust retains
neither input nor output pointers. Every sector reaches Rust through the
validated block read callback; the parser never touches storage-controller
registers or firmware protocols.

The current kernel shell is the only caller. Sharing the parser after scheduler
or SMP work will require C-side synchronization.

## Descriptor layout

`argus_fat32_v1_t` is exactly 88 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `abi_version` | Must be `ARGUS_FAT32_ABI_VERSION` (`1`). |
| 4 | 4 | `struct_size` | Must be 88. |
| 8 | 24 | `name` | NUL-terminated printable component name. |
| 32 | 4 | `state_size` | Required opaque state bytes. |
| 36 | 4 | `state_alignment` | Required power-of-two alignment. |
| 40 | 4 | `max_path` | Maximum accepted path bytes. |
| 44 | 4 | `max_read` | Maximum file bytes copied per call. |
| 48 | 8 | `reserved` | Both words must be zero. |
| 56 | 8 | `mount` | Validate BPB geometry and initialize state. |
| 64 | 8 | `info` | Return mounted volume geometry. |
| 72 | 8 | `entry` | Enumerate a live root entry. |
| 80 | 8 | `read` | Locate and read one root file. |

`argus_fat32_info_v1_t` is a 40-byte output structure containing sector size,
cluster size, FAT count, root cluster, total sectors, data-cluster count, and the
first data-sector LBA.

## Parser validation

Mount rejects an invalid boot signature, non-FAT32 BPB fields, unsupported sector
or cluster sizes, an out-of-range active FAT selected by the extended flags,
arithmetic overflow, a volume beyond its block device, a data region below the
FAT32 cluster threshold, a root cluster outside the data region, or a FAT too
small to address every cluster. When mirroring is disabled, reads use the FAT
selected by the BPB rather than assuming the first copy.

Directory and file reads reject invalid clusters, bad/reserved chain values,
premature end-of-chain markers, directory scans beyond their bound, file-chain
cycles, unsupported directory reads, undersized output buffers, and block I/O
failures.

## Status values

| Value | Name | Meaning |
|---:|---|---|
| 0 | `ARGUS_FAT32_OK` | Operation completed. |
| -1 | `ARGUS_FAT32_NOT_FOUND` | Root path or enumeration index is absent. |
| -2 | `ARGUS_FAT32_INVALID` | Invalid state, pointer, path, or argument. |
| -3 | `ARGUS_FAT32_UNSUPPORTED` | Valid concept outside ABI v1's feature set. |
| -4 | `ARGUS_FAT32_CORRUPT` | Inconsistent BPB, directory, or FAT chain. |
| -5 | `ARGUS_FAT32_IO_ERROR` | The block callback failed. |
| -6 | `ARGUS_FAT32_BUFFER_TOO_SMALL` | Output is smaller than the file. |

Boot tests mount either registered block backend, validate its geometry, find
`/HELLO.TXT` without relying on directory order, exercise short-buffer reporting
and case-insensitive lookup, compare all file bytes, and reject invalid, nested,
and missing paths. QEMU requires the selected device to be `ahci0`, so its shell
listing and read prove that the Rust parser consumed sectors delivered by the
post-firmware AHCI driver. The sparse fixture remains a fallback for machines
without supported SATA hardware.

ABI v1 is frozen. Long filenames, subdirectories, or a larger read model should
arrive through an additive new ABI rather than changing this descriptor.
