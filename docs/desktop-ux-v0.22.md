# ArgusOS desktop interaction correction v0.22

ArgusOS v0.22 changes the retained desktop from a diagnostic window pile into a
small, predictable workstation. The compositor still owns seven fixed surfaces,
but desktop policy permits no more than one of them to be visible at a time.

## Startup and navigation

Desktop startup shows only the olive workspace and bottom panel. No application
is focused and no window covers the workspace. The panel always contains an
`APPS` control. It adds one current-task control only while a window is visible.

`APPS` or Tab replaces the current window with the Applications launcher. Up and
Down select a row, Enter opens it, number keys 1 through 6 provide direct
selection, and a mouse click opens a row. Opening another application hides the
previous window while retaining its state.

Escape or the current-task control minimizes the visible window and returns to
the empty desktop. Printable keyboard input is consumed by noninteractive System
and Files windows instead of being typed invisibly into the hidden console.
Serial access to the kernel shell remains independent of graphical focus.

## Window controls

Every window has two functional title-bar controls:

- minimize hides the window without discarding its state
- close hides the window and, for a C++ user application, releases its process,
  address space, stack, image, and shared surface through the existing lifecycle

Reopening a closed user application creates a fresh process. Kernel-owned utility
windows have no process to destroy, so their close action hides the surface.

Title dragging remains bounded to the work area. The compositor damages both the
old and new bounds, and invisible windows do not participate in hit testing or
composition.

## Pointer boundary

The software pointer's complete 12 by 16 pixel bitmap is clamped inside the GOP
framebuffer. Its hotspot can reach `(0,0)`, including the top edge, while its
body cannot disappear beyond the right or bottom edge.

ArgusOS currently consumes a relative PS/2 mouse. A host QEMU window must capture
relative input or the host cursor can leave the window before the guest reaches
an edge. `make run` is the supported interactive path. It:

- locates the installed matching OVMF code and variables images
- selects KVM when `/dev/kvm` is accessible and otherwise uses TCG
- uses a temporary writable OVMF variables copy
- enables GTK grab-on-hover and hides the duplicate host cursor
- retains the two-NIC disconnected test topology

`Ctrl+Alt+G` releases the QEMU pointer grab.

## Enforced invariants

The desktop and compositor diagnostics require:

- zero visible windows when the active surface is `DESKTOP`
- exactly one visible window when an application is active
- that visible window to be the active compositor surface
- the pointer origin and complete bitmap to remain inside the framebuffer
- every hidden window to be excluded from hit testing and composition

The QEMU smoke run captures the empty desktop, drives the pointer to `(0,0)`,
opens the launcher, selects Calculator, Notes, and Snake individually, verifies
their focused input, closes Calculator through the pointer and verifies process
teardown, then uses Escape to return to zero visible windows.

## Remaining limits

Windows remain fixed-size and kernel-composited. There is no resize, icon grid,
desktop file placement, user-space display server, USB HID, or persistence for
Notes. Those features are not represented by inactive or decorative controls.
