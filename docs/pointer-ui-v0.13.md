# ArgusOS pointer and movable window v0.13

ArgusOS v0.13 adds the first pointer interaction path and removes decorative
desktop elements that did not represent real functionality. The interface now
contains a plain desktop field, one Kernel Console window, one active task entry,
and a software pointer.

## PS/2 mouse path

The i8042 driver enables and tests the auxiliary port while CPU interrupts are
disabled. It applies device defaults, enables streaming, and routes legacy IRQ12
through the I/O APIC to vector `0x42`. Keyboard IRQ1 remains on vector `0x41`.

The shared interrupt drain routes bytes using the controller auxiliary-data bit.
Mouse bytes are synchronized on the always-set packet bit, decoded as signed
three-byte relative packets, converted to screen-oriented deltas, and delivered
through a bounded single-producer/single-consumer queue. Packet and overflow
counters are visible through the `input` shell command. Polling remains available
when routing fails.

## Pointer rendering

The pointer renderer saves the small rectangle beneath its bitmap before drawing
and restores those pixels before movement or terminal output. The kernel shell
hides the pointer while it writes framebuffer text, preventing stale saved pixels
from erasing new glyphs.

No pointer drawing occurs in interrupt context. IRQ12 only queues decoded events;
the shell loop consumes events and updates the framebuffer.

## Hit testing and dragging

A left-button press inside the real title-bar rectangle starts a drag. Relative
motion moves the fixed-size window within the desktop area above the task panel.
The framebuffer mover handles overlapping source and destination rectangles in
the correct direction, then fills only the exposed portion of the old window.

The terminal's clipped console region moves by the same delta without clearing
its contents. This preserves live shell output while the frame moves.

## Test boundary

`make smoke` now requires both PS/2 keyboard and mouse IRQ markers. QEMU injects
relative pointer motion and a left-button drag, then requires `DESKTOP_DRAG_OK`
from the `ui` command. The test stores an initial framebuffer and a second capture
after the drag, and validates the defining palette in both.

## Remaining limits

- one kernel-owned window and one pointer
- no resize, close, minimize, focus switching, or stacking
- no double buffer, damage tracker, or general compositor
- no USB HID pointer path
- no user-space display protocol

The next UI work should build a small damage-aware compositor and surface API.
Ring 3, syscalls, and scheduling should then move durable window ownership into a
C++ user-space display server.

The compositor and retained-surface milestone is completed in v0.14 and
documented in `docs/surface-compositor-v0.14.md`.
