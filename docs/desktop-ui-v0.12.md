# ArgusOS desktop prototype v0.12

ArgusOS v0.12 adds the first graphical desktop slice. It is a static,
kernel-owned scene around the existing native shell, not a window manager,
display server, or application security boundary.

## What is implemented

- an original muted olive, warm gray, slate, and near-black palette
- square one-pixel raised and recessed chrome
- a desktop identity line, icon rail, terminal frame, and bottom panel
- a region-aware framebuffer terminal inside the terminal frame
- rectangular clipped scrolling that cannot overwrite surrounding chrome
- terminal-only `clear` behavior after the desktop becomes active
- deterministic redraw through the `desktop` shell command
- serial mirroring and serial input in both the UEFI monitor and native shell
- QEMU GOP coverage, automatic PPM capture, and palette checks during `make smoke`

The visual tokens and guardrails live in `DESIGN.md`. Product intent and explicit
anti-references live in `PRODUCT.md`.

## Ownership

`desktop.c` owns only scene layout and visual primitives. `console.c` owns text
cursor state, glyph placement, region clipping, and scroll behavior. `gop.c`
owns validated framebuffer access and rectangular pixel movement. The serial
console remains independent of the framebuffer scene.

No input event is routed to desktop chrome yet. The icons and panel communicate
the intended layout but are not interactive controls. Shell commands remain the
only user interaction.

## Why this is in the kernel for now

ArgusOS does not yet have ring 3, scheduling, per-process address spaces, or a
display-server protocol. The prototype is intentionally small and disposable:
it validates the framebuffer geometry, terminal viewport, palette, density, and
visual identity before the durable C++ user-space UI is introduced.

The kernel will continue to own hardware and low-level display primitives. Once
Stage J exists, the scene and widget behavior should move behind a minimal
user-space display protocol. C++ can then own the retained window and widget
tree without inheriting kernel privileges.

## Current limitations

- no mouse pointer, hit testing, focus changes, dragging, or window controls
- no double buffer or damage tracking
- no general surface compositor
- one fixed terminal window
- the built-in 5x7 font maps lowercase to uppercase
- desktop icons are visual landmarks, not launch targets
- the layout targets the current GOP resolution and scales at one or two integer pixels

That next milestone is completed in v0.13 and documented in
`docs/pointer-ui-v0.13.md`.
