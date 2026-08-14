# ArgusOS user application platform v0.17

ArgusOS v0.17 replaces the one-off Snake frame call with a small graphical app
contract and ships three persistent C++ user processes: Snake, Calculator, and
Notes. The Applications window provides keyboard and mouse discovery and launch.

## Process and image model

The two boot probes remain PIDs 1 and 2. The persistent apps use PIDs 3 through 5
and app IDs 1 through 3. Each app is a separately linked, position-independent
flat image embedded in the EFI payload. `user/app.ld` fixes the entry address at
`0x0000008000001000`, rejects writable static storage, and enforces a 16 KiB code
limit.

Every process owns a private CR3, read-only executable code pages, one writable
NX stack page, an unmapped stack guard, and a separate writable NX app surface.
The surface mappings resolve to different physical pages in every address space.

## Shared surface ABI

`src/app_abi.h` defines a 320x224, one-byte-per-pixel surface at
`0x0000008000400000`. A fixed eight-entry palette keeps user images independent
of GOP channel order and limits what an untrusted client can ask the compositor
to display. The palette contains the existing terminal, chrome, title, field,
and muted LCD roles from `DESIGN.md`.

Syscall 7 accepts `argus_app_present_v1_t`. The kernel requires the ABI magic and
version, zero flags, a strictly increasing nonzero sequence, a readable request,
and palette indices below eight across the whole surface. The process cannot map
or write the compositor's retained 32-bit window surface. After validation, the
desktop copies and translates the indexed pixels into that kernel-owned surface.

This is a shared producer buffer with a validated present boundary. It is not a
general IPC system and it is not a zero-copy display server.

## Events and scheduling

Focused PS/2 keys enter a bounded queue owned by the selected process. Syscall 6
polls that queue and syscall 5 returns APIC ticks. Only the focused persistent app
is cooperatively dispatched after its initial frame. COM1 remains reserved for
the kernel shell, so graphical app focus cannot take away recovery diagnostics.

There is no blocking wait yet. Each app polls, redraws only when state changes,
presents a new sequence, and yields.

## Shared C++ runtime

`user/app_runtime.cpp` supplies only the facilities these apps need:

- raw syscall wrappers
- indexed pixel, rectangle, frame, and text drawing
- the existing uppercase 5x7 bitmap glyph set
- bounded signed and unsigned decimal formatting
- surface clear and versioned present

It has no standard library, heap, exceptions, RTTI, thread statics, unwinder,
global constructors, direct hardware access, or writable global state.

## Installed applications

### Snake

Snake retains its 20x14 board, collision, food, score, restart, arrow, and WASD
behavior. The game now draws its own complete indexed surface rather than sending
a game-specific cell structure for the kernel to interpret.

### Calculator

Calculator accepts digits, `+`, `-`, `*`, `/`, Enter or `=`, Backspace, and `C`.
It uses bounded integer arithmetic, reports division or range errors, and renders
a working keypad plus a muted LCD result field. The QEMU smoke test computes
`12 + 3` and verifies the displayed result through the framebuffer capture.

### Notes

Notes is a 512-byte in-memory editor with printable input, wrapping, Enter, and
Backspace. Its window says `VOLATILE MEMORY` because no user filesystem capability
exists and closing or rebooting cannot preserve the buffer.

## Applications launcher

The kernel-hosted Applications surface lists Console, System, Files, Snake,
Calculator, and Notes. It labels kernel services and C++ user processes honestly.
Up/Down changes the selection, Enter opens it, 1 through 6 launch directly, and a
mouse click on a row focuses the target. The bottom panel remains a running-window
switcher and exposes the launcher through `APPS`.

## Current limits and next step

The app images are compiled into the kernel payload. A user fault still lacks a
contained terminate-and-recover path. Scheduling is cooperative, input polling
burns dispatches, Notes cannot save, and the remaining utilities still execute in
the kernel.

The next platform step is an ELF loader with process creation, teardown, and user
fault termination, followed by blocking event waits and filesystem capabilities.
A web browser is not yet credible: it requires networking, sockets, DNS, TLS, and
at least a bounded text/HTML document engine first.
