# ArgusOS retained surfaces and utility apps v0.14

ArgusOS v0.14 replaces direct window movement in the physical framebuffer with
a small retained-surface compositor. It remains kernel-hosted by design. This
phase establishes graphics and input contracts before processes can safely own
windows.

## Surface layer

`surface.c` owns heap-backed 32-bit pixel buffers. Each surface has fixed width,
height, stride, and a bounded dirty rectangle. Its operations include clipped
pixel writes, rectangle fills, bitmap text, and overlap-safe vertical scrolling.

The surface self-test allocates a small buffer, verifies clipped drawing and
scroll fill behavior, checks damage consumption, and releases the allocation.
Desktop startup stops if this test fails.

## Compositor

`compositor.c` owns screen positions, visibility, z-order, a fixed-capacity
damage list, and the panel surface. Composition resolves every damaged pixel in
this order:

1. desktop field
2. visible windows from back to front
3. task panel

Moving a window damages its old and new bounds. Raising a window damages the
affected window bounds. Surface-local damage is translated to screen space.
The software pointer remains a final framebuffer overlay and is hidden before a
composition pass so its saved background never becomes stale.

The implementation deliberately avoids a full-screen back buffer. Retained app
surfaces preserve content, while bounded damage keeps framebuffer work local.

## Built-in applications

All three applications are real kernel-hosted surfaces:

- **Kernel Console** contains the live serial-mirrored native shell. The console
  now draws and scrolls inside its retained surface instead of drawing directly
  into the physical framebuffer.
- **System** reports current physical pages, kernel heap usage, APIC ticks, and
  keyboard and pointer modes. It refreshes periodically from live subsystem state.
- **Files** enumerates the Rust RAMFS and the mounted read-only Rust FAT32 root.
  Successful RAMFS writes and removals refresh the view.

The bottom panel contains only these running tasks. Clicking a task or window
changes focus and z-order. Any title bar can be dragged inside the work area.
The shell commands `apps`, `focus console`, `focus system`, and `focus files`
provide equivalent keyboard and serial control.

## Language boundary

C is the correct language for this phase because the compositor is still linked
inside the kernel and directly coordinates the heap, framebuffer, console, and
interrupt-produced input. The Files application already consumes safe bounded
data components implemented in `no_std` Rust through versioned C ABIs.

C++ is targeted for v0.16. The v0.15 process phase must first provide ring 3,
syscalls, context switching, scheduling, per-process address spaces, and an
executable loader. C++ can then own a user-space display server, retained widget
tree, and application toolkit without moving complex object lifetimes into the
kernel.

## Verification

The QEMU smoke test now requires:

- `SURFACE_SELF_TEST_PASS`
- `COMPOSITOR_ONLINE`
- `DESKTOP_APPS_ONLINE`
- `COMPOSITOR_APPS_OK`
- `DESKTOP_DRAG_OK`
- `COMPOSITOR_DAMAGE_OK`
- a mouse focus change to `SYSTEM`

It captures the initial three-app desktop, the moved Console, and the focused
System state. All captures are checked for the normative ArgusOS palette.

## Current limits

- surfaces and applications are kernel-owned
- no process isolation or user-space display protocol
- no close, resize, minimize, or dynamic application launch
- no alpha blending, shaped windows, or font assets loaded from disk
- fixed upper bounds of eight windows and sixteen pending damage rectangles

The next implementation phase is the v0.15 process foundation. It should keep
this compositor as a reference backend while establishing the syscall and shared
surface contracts required by the v0.16 C++ display server.
