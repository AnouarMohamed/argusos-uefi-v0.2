# ArgusOS C++ Snake user app v0.16

ArgusOS v0.16 adds its first persistent user application: a playable block Snake
game written in freestanding C++20. The game executes at ring 3 in its own address
space. The kernel still owns input devices, scheduling, frame validation, retained
surfaces, composition, and the physical framebuffer.

## User image

`user/snake.cpp` is compiled for `x86_64-none-elf` without a hosted runtime. The
build disables exceptions, RTTI, stack protectors, thread-safe statics, unwinding,
the red zone, SSE, and compiler builtin calls. It does not use the C++ standard
library, dynamic allocation, global constructors, or writable static state.

`user/snake.ld` links the image at `0x0000008000001000`, the code address shared by
Argus user spaces. Linker assertions reject code larger than four 4 KiB pages and
reject `.data` or `.bss`. `objcopy` extracts the linked text and read-only data as a
flat image, then `src/user_images.S` embeds those bytes in the EFI payload.

At boot the process loader copies the image into PMM-owned pages, maps them
read-only and executable, maps one writable NX stack page above an unmapped guard,
and starts the C++ entry point with PID 3. The process presents its first frame and
yields. It remains ready but is scheduled only while the Snake window has focus.

## App ABI

The v1 game boundary adds three syscalls:

| Number | Operation | Result or payload |
| --- | --- | --- |
| 5 | clock ticks | current local-APIC tick count |
| 6 | input poll | one focused key value, or zero |
| 7 | Snake present | validated `argus_snake_frame_v1_t` snapshot |

The frame has a magic value, ABI version, monotonic sequence, game state, score,
length, fixed 20 by 14 dimensions, and 280 bounded cell values. Before accepting a
frame, the kernel checks its mapped user range, exact byte size, metadata, cell
values, one head, one food marker, snake cell count, and increasing sequence.

This ABI is deliberately narrow. The process cannot write to a surface or the
framebuffer, access a device, allocate kernel memory, or forge arbitrary drawing
commands.

## Gameplay and interface

The C++ process owns movement, collision, growth, food placement, score, restart,
and deterministic fixed-capacity state. Arrow keys or WASD steer; `R` restarts.
Opposite-direction turns are rejected.

The kernel renders accepted frames in a fourth retained app surface titled
`SNAKE`. It uses the standard one-pixel Argus chrome and task behavior. The content
uses a muted LCD field, dark block cells, a larger head, and a hollow food marker.
Only score, controls, and game-over state are displayed.

PS/2 keyboard input is routed to the game only while its window is focused. Serial
input always remains attached to the kernel monitor, so diagnostics and recovery
remain available during gameplay. Clicking another task returns physical keyboard
input to the console.

## Validation

Boot requires the C++ image to enter ring 3, query its PID and clock, submit a valid
initial frame, and yield. The QEMU smoke test then:

1. confirms three processes and the persistent C++ app;
2. focuses Snake through the real desktop task model;
3. injects an extended PS/2 direction key;
4. requires that the user process consume the key and submit a later frame;
5. validates compositor state and captures the focused game window.

CI also checks that the C++ object has no undefined symbols, the flat image remains
within 16 KiB, and the ELF entry address matches the process mapping.

## Deliberate limits

Snake is a real user process, but it is not loaded from a filesystem and the game
frame ABI is not a generic window-system protocol. Scheduling remains cooperative.
There is no blocking input wait, preemption, user exception recovery, IPC namespace,
shared surface mapping, or dynamic process lifecycle yet.

The next phase should generalize this proven path into an app channel with shared
surfaces and events, then move the terminal behind it before relocating compositor
ownership to a user-space display server.
