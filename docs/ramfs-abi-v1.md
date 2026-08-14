# ArgusOS RAMFS ABI v1

This document defines the bounded C/Rust boundary used by the in-memory
filesystem introduced in ArgusOS v0.9. The C layout in
[`src/ramfs_abi.h`](../src/ramfs_abi.h) is the source of truth. The `no_std`
Rust implementation mirrors it with `#[repr(C)]` in
[`src/rust_ramfs.rs`](../src/rust_ramfs.rs).

## Scope

RAMFS v1 is a volatile, fixed-capacity filesystem for bringing up the storage
namespace and shell interface before a disk driver exists. It supports create or
replace, read, enumerate, and remove. It has no allocator, persistence,
directories, permissions, timestamps, links, or concurrent writers.

The current limits are deliberately small:

- 16 files;
- 48 path bytes per file;
- 1,024 data bytes per file;
- one C-owned, 16-byte-aligned state buffer;
- no ownership transfer across the ABI.

Slash-separated paths are accepted as names, but v1 stores a flat collection;
directory traversal is not implemented.

## Platform and runtime contract

- Architecture: x86-64.
- Object format: Microsoft COFF linked into `BOOTX64.EFI`.
- Calling convention: Microsoft x64 C ABI through Rust `extern "C"` functions.
- Rust runtime: `#![no_std]`, no allocator, no TLS, no constructors, and
  `panic=abort`.
- The Rust object may reference only kernel-supplied `memcpy` and `memset`.
- C owns the state allocation and calls `initialize` before every other method.
- The filesystem is currently called only by the single kernel-shell thread.
  A scheduler or SMP access will require a C-side lock before sharing this state.

## Descriptor layout

`argus_ramfs_v1_t` is exactly 96 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `abi_version` | Must be `ARGUS_RAMFS_ABI_VERSION` (`1`). |
| 4 | 4 | `struct_size` | Must be 96. |
| 8 | 24 | `name` | NUL-terminated printable component name. |
| 32 | 4 | `state_size` | Required opaque state bytes. |
| 36 | 4 | `state_alignment` | Required power-of-two alignment. |
| 40 | 4 | `max_files` | Maximum number of live files. |
| 44 | 4 | `max_path` | Maximum path length in bytes. |
| 48 | 4 | `max_data` | Maximum data length per file. |
| 52 | 4 | `reserved` | Must be zero. |
| 56 | 8 | `initialize` | Initialize C-owned state. |
| 64 | 8 | `write` | Create or replace a file. |
| 72 | 8 | `read` | Copy out file bytes. |
| 80 | 8 | `entry` | Enumerate a file by live ordinal. |
| 88 | 8 | `remove` | Remove a file. |

Both languages assert the descriptor size. C additionally rejects unexpected
limits, alignment, reserved fields, or null function pointers before executing
the Rust implementation.

## Input rules

Paths must:

- begin with `/` and contain at least one nonempty segment;
- be at most 48 bytes;
- contain printable non-space ASCII;
- not end in `/`;
- not contain repeated separators, backslashes, `.` segments, or `..` segments.

File data is an arbitrary byte sequence from zero through 1,024 bytes. Input
pointers are borrowed for the duration of one call and are never retained.
Output buffers remain C-owned. The Rust implementation checks all declared
lengths before copying and revalidates the opaque state's live count, stored
lengths, and paths before every operation.

## Status values

| Value | Name | Meaning |
|---:|---|---|
| 0 | `ARGUS_RAMFS_OK` | Operation completed. |
| -1 | `ARGUS_RAMFS_NOT_FOUND` | Path or enumeration index does not exist. |
| -2 | `ARGUS_RAMFS_INVALID` | Invalid pointer, state, path, or argument. |
| -3 | `ARGUS_RAMFS_NO_SPACE` | File slots or per-file capacity exhausted. |
| -4 | `ARGUS_RAMFS_BUFFER_TOO_SMALL` | Output capacity is insufficient. |

For `read` and `entry`, the required output length is reported before returning
`ARGUS_RAMFS_BUFFER_TOO_SMALL`.

## Validation

Boot-time tests cross the ABI to require invalid-path rejection, fill all file
slots, observe exhaustion, detect a short output buffer, read back data, remove
and reuse a slot, and enumerate every live file. The state is then reset and
seeded with `/README` and `/etc/motd`. QEMU subsequently performs a separate
shell-driven write/read/remove/not-found round trip.

ABI v1 is frozen. Incompatible layouts or semantics require a new descriptor
type and entry point.
