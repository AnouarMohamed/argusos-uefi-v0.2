---
name: ArgusOS Desktop
description: A quiet, direct workstation interface for ArgusOS.
colors:
  field-olive: "#4c5148"
  warm-chrome: "#b8b4a5"
  chrome-highlight: "#d3d0c2"
  chrome-shadow: "#6e6c64"
  terminal-ink: "#171a17"
  terminal-text: "#c9c8b5"
  slate-title: "#4e5869"
  near-ink: "#242724"
  lcd-field: "#919478"
  lcd-ink: "#2b2e27"
typography:
  title:
    fontFamily: "Argus 5x7 Bitmap, monospace"
    fontSize: "14px"
    fontWeight: 700
    lineHeight: 1
    letterSpacing: "1px"
  body:
    fontFamily: "Argus 5x7 Bitmap, monospace"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.28
    letterSpacing: "normal"
  label:
    fontFamily: "Argus 5x7 Bitmap, monospace"
    fontSize: "12px"
    fontWeight: 700
    lineHeight: 1
    letterSpacing: "1px"
rounded:
  square: "0px"
spacing:
  hairline: "1px"
  xs: "2px"
  sm: "4px"
  md: "8px"
  lg: "12px"
components:
  active-task:
    backgroundColor: "{colors.warm-chrome}"
    textColor: "{colors.near-ink}"
    typography: "{typography.label}"
    rounded: "{rounded.square}"
    padding: "4px 8px"
  terminal-window:
    backgroundColor: "{colors.terminal-ink}"
    textColor: "{colors.terminal-text}"
    typography: "{typography.body}"
    rounded: "{rounded.square}"
    padding: "8px"
  utility-window:
    backgroundColor: "{colors.terminal-ink}"
    textColor: "{colors.terminal-text}"
    typography: "{typography.body}"
    rounded: "{rounded.square}"
    padding: "8px"
  game-window:
    backgroundColor: "{colors.lcd-field}"
    textColor: "{colors.lcd-ink}"
    typography: "{typography.body}"
    rounded: "{rounded.square}"
    padding: "8px"
---

# Design System: ArgusOS Desktop

## 1. Overview

**Creative North Star: "The Quiet Workstation"**

ArgusOS should feel like a maintained workstation in a dim room: practical,
slightly severe, and built for legibility. Its visual language uses economical
layout, restrained color, and tactile one-pixel system chrome without making an
era or aesthetic reference part of the product identity.

It explicitly rejects glossy Windows XP imitation, literal imageboard branding,
neon hacker theater, glass, gradients, rounded cards, and decorative animation.

**Key Characteristics:**

- Muted olive field with warm gray system chrome
- Square corners and crisp one-pixel bevels
- Small bitmap type with compact, keyboard-first labels
- One desaturated title color
- Only real windows, actions, and state labels

## 2. Colors

The palette is low-chroma and warm enough to avoid modern blue-black developer
tool styling while preserving clear terminal contrast.

### Primary

- **Dusty Workbench Olive** (#4c5148): desktop field and the dominant visual atmosphere.
- **Aged System Chrome** (#b8b4a5): panels, controls, and window frames.

### Secondary

- **Faded Slate Title** (#4e5869): active title bars and keyboard focus.

### Neutral

- **Terminal Ink** (#171a17): terminal and deep content surfaces.
- **Phosphor Paper** (#c9c8b5): terminal text and high-contrast labels.
- **Chrome Highlight** (#d3d0c2): top and left bevel edges.
- **Chrome Shadow** (#6e6c64): bottom and right bevel edges.
- **Near Ink** (#242724): chrome text and pointer outline.
- **Muted LCD Field** (#919478): the Snake board's low-glare monochrome field.
- **LCD Ink** (#2b2e27): game cells, score, controls, and state text.

### Named Rules

**The Dust Rule.** No fully saturated color appears anywhere. Accent color stays
under ten percent of a screen and always communicates state.

## 3. Typography

**Display Font:** Argus 5x7 Bitmap (monospace fallback)
**Body Font:** Argus 5x7 Bitmap (monospace fallback)
**Label/Mono Font:** Argus 5x7 Bitmap

**Character:** One pixel vocabulary serves chrome and terminal output.
Two-times integer scaling preserves hard edges on larger framebuffers.

### Hierarchy

- **Title** (700, 14px, 1): concise window titles.
- **Body** (400, 14px, 1.28): terminal text, capped by the terminal viewport.
- **Label** (700, 12px, 1px tracking): active tasks and short controls.

### Named Rules

**The Integer Pixel Rule.** Glyphs are scaled only by whole numbers and never
smoothed, stretched, or shadowed.

## 4. Elevation

There are no blurred shadows. Depth comes from one-pixel light and dark edges,
as it did in classic desktop chrome. Active controls invert those edges to look
physically pressed.

### Named Rules

**The One-Pixel Bevel Rule.** Every raised or recessed edge uses exactly one
highlight and one shadow line. Never stack ornamental borders.

## 5. Components

### Buttons

- **Shape:** square (0px radius), compact and text-led.
- **Primary:** Aged System Chrome with Near Ink and 4px by 8px padding.
- **Hover / Focus:** Faded Slate outline plus a textual focus cue.
- **Active:** inverted one-pixel bevel, no scaling animation.

### Cards / Containers

- **Corner Style:** square (0px radius).
- **Background:** use a single field or chrome surface.
- **Shadow Strategy:** no shadow; one-pixel structural bevel only.
- **Border:** full perimeter, never an accent stripe.
- **Internal Padding:** 8px or 12px according to density.

### Inputs / Fields

- **Style:** Terminal Ink field, Phosphor Paper text, one-pixel recessed frame.
- **Focus:** visible Faded Slate outline and text cursor.
- **Error / Disabled:** pair color with explicit text.

### Navigation

The bottom panel shows running tasks only. Each item must focus and raise its
window. It does not contain a launcher or status copy until those surfaces are
functional. Active items use an inverted bevel.

### Terminal Window

The movable terminal is a retained application surface. Its concise title names
the surface, its contents are real serial-mirrored output, and its frame leaves
enough field visible for pointer movement and dragging.

### Utility Windows

System and Files use the same retained surface, title, content inset, focus, and
dragging rules as the terminal. System displays live allocator, timer, and input
state. Files displays real RAMFS and FAT32 entries. They do not invent actions or
status that the kernel cannot provide.

### Game Window

Snake uses the same window frame, focus, task, and drag behavior as every other
surface. Its content is a muted monochrome LCD field with block cells, one hollow
food marker, score, compact keyboard controls, and an explicit game-over state.
The title is simply `SNAKE`; no era or device branding appears in the UI.

### Focus and Composition

Faded Slate marks the focused title only. Inactive titles use Chrome Shadow.
Clicking a title or task raises the window. Window movement is immediate and
uses damage composition without decorative animation.

## 6. Do's and Don'ts

### Do:

- **Do** use #4c5148 as the dominant field and #b8b4a5 for system chrome.
- **Do** use square corners and exact one-pixel highlight/shadow edges.
- **Do** name only real surfaces, actions, and system state.
- **Do** keep the terminal readable with #c9c8b5 on #171a17.
- **Do** preserve keyboard and serial access for every important action.
- **Do** use the same focus, dragging, and content-inset behavior for every window.

### Don't:

- **Don't** make ArgusOS a glossy Windows XP clone or a literal imageboard clone.
- **Don't** use era labels, copied artwork, fake system state, or nonfunctional chrome.
- **Don't** use neon cyberpunk accents, gradients, glassmorphism, or rounded cards.
- **Don't** use a colored side stripe on any panel or item.
- **Don't** animate anything that does not communicate a state change.
- **Don't** add close, resize, or launch controls before those actions work.
