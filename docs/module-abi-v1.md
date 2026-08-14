# ArgusOS internal module ABI v1

This document defines the narrow binary boundary used by the first mixed-language
ArgusOS module. The source of truth for its C layout is
[`src/module_abi.h`](../src/module_abi.h). Rust mirrors that layout with
`#[repr(C)]` in [`src/rust_probe.rs`](../src/rust_probe.rs).

## Scope

ABI v1 exists to prove that a separately compiled, freestanding module can be
validated and called safely by the C kernel. It is an internal, statically linked
ABI, not a userspace ABI and not a dynamic module loader.

The first module exposes one FNV-1a checksum function. It does not participate in
boot, page-table construction, allocation, interrupt dispatch, device I/O, or
panic handling. Those remain C/assembly responsibilities.

## Platform contract

- Architecture: x86-64 only.
- Object format: Microsoft COFF, linked into `BOOTX64.EFI` by `lld-link`.
- Calling convention: Microsoft x64 C ABI (`extern "C"` on the
  `x86_64-pc-windows-msvc` Rust target).
- Integer representation: fixed-width unsigned integers from `<stdint.h>`.
- Structure representation: C layout with 8-byte maximum alignment.
- Unwinding: forbidden across the boundary; Rust is built with `panic=abort`.
- Runtime: no standard library, allocator, TLS, constructors, or firmware calls.

## Descriptor layout

`argus_module_v1_t` is exactly 40 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `abi_version` | Must equal `ARGUS_MODULE_ABI_VERSION` (`1`). |
| 4 | 4 | `struct_size` | Must equal 40 for ABI v1. |
| 8 | 24 | `name` | Non-empty, NUL-terminated ASCII module name. |
| 32 | 8 | `checksum` | Microsoft x64 C function pointer. |

The C `_Static_assert` and the Rust `size_of` value guard the structure size on
both sides. The kernel rejects a descriptor with the wrong version, size, name,
or function pointer before calling it.

## Function contract

```c
uint64_t checksum(const uint8_t *bytes, uint64_t length);
```

- The input is borrowed and read-only; ownership never crosses the boundary.
- For a nonzero length, `bytes` must address at least `length` readable bytes.
- `(NULL, 0)` is valid and produces the checksum of an empty byte string.
- `(NULL, nonzero)` is invalid and the current module returns zero defensively.
- The function must not retain the pointer or call back into the kernel.

The exported entry point returns a borrowed pointer to an immutable descriptor:

```c
const argus_module_v1_t *argus_rust_module_entry(void);
```

The descriptor has static lifetime and is owned by the module.

## Versioning rules

ABI v1 is frozen once committed. Any field reorder, field-type change, calling
convention change, or semantic incompatibility requires a new ABI version and a
new descriptor type. A future version may use `struct_size` for append-only
optional fields, but v1 requires an exact size so accidental layout drift fails
closed.

Every new module must have:

1. a descriptor validation path in C;
2. a deterministic boot-time self-test that crosses the ABI;
3. a serial marker covered by the QEMU smoke test;
4. an explicit ownership and failure contract in this document;
5. no undefined symbols other than interfaces intentionally supplied by ArgusOS.

## Rust policy

Rust modules use `#![no_std]`, `#[repr(C)]`, fixed-width ABI types, and
`extern "C"`. They may use `unsafe` only where the ABI requires raw-pointer
access, with a local safety comment. Kernel allocators are not exposed in v1, so
Rust code cannot allocate. Panics, unwinding, architecture-specific entry code,
and direct access to firmware-owned data are outside this ABI.
