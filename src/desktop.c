#include "desktop.h"
#include "console.h"
#include "font5x7.h"
#include "gop.h"

#define POINTER_WIDTH 12u
#define POINTER_HEIGHT 16u

typedef struct {
    uint32_t field;
    uint32_t chrome;
    uint32_t highlight;
    uint32_t shadow;
    uint32_t terminal;
    uint32_t terminal_text;
    uint32_t title;
    uint32_t ink;
} desktop_colors_t;

static const uint16_t pointer_outer[POINTER_HEIGHT] = {
    0x800u, 0xC00u, 0xE00u, 0xF00u,
    0xF80u, 0xFC0u, 0xFE0u, 0xFF0u,
    0xFF8u, 0xFC0u, 0xDC0u, 0x8E0u,
    0x060u, 0x070u, 0x030u, 0x000u
};

static const uint16_t pointer_inner[POINTER_HEIGHT] = {
    0x000u, 0x400u, 0x600u, 0x700u,
    0x780u, 0x7C0u, 0x7E0u, 0x7F0u,
    0x7C0u, 0x780u, 0x4C0u, 0x040u,
    0x020u, 0x020u, 0x000u, 0x000u
};

static desktop_colors_t colors;
static uint32_t scale;
static uint32_t panel_height;
static uint32_t title_height;
static uint32_t window_x;
static uint32_t window_y;
static uint32_t window_width;
static uint32_t window_height;
static uint32_t terminal_offset_x;
static uint32_t terminal_offset_y;
static uint32_t terminal_width;
static uint32_t terminal_height;

static int pointer_enabled;
static int pointer_visible;
static int pointer_initialized;
static int dragging;
static uint32_t pointer_x;
static uint32_t pointer_y;
static uint32_t drag_offset_x;
static uint32_t drag_offset_y;
static uint8_t pointer_buttons;
static uint64_t drag_moves;
static uint32_t pointer_saved[POINTER_WIDTH * POINTER_HEIGHT];
static uint32_t pointer_saved_width;
static uint32_t pointer_saved_height;

static uint32_t text_width(const char *text, uint32_t text_scale) {
    uint32_t count = 0;
    while (*text++) ++count;
    return count * 6u * text_scale;
}

static void draw_glyph(
    char c,
    uint32_t x,
    uint32_t y,
    uint32_t text_scale,
    uint32_t color
) {
    for (uint32_t row = 0; row < 7u; ++row) {
        uint8_t bits = font5x7_row(c, row);
        for (uint32_t col = 0; col < 5u; ++col) {
            if (bits & (1u << (4u - col)))
                gop_fill_rect(
                    x + col * text_scale,
                    y + row * text_scale,
                    text_scale,
                    text_scale,
                    color
                );
        }
    }
}

static void draw_text(
    const char *text,
    uint32_t x,
    uint32_t y,
    uint32_t text_scale,
    uint32_t color
) {
    while (*text) {
        draw_glyph(*text++, x, y, text_scale, color);
        x += 6u * text_scale;
    }
}

static void draw_bevel(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t fill,
    int raised
) {
    if (width < 2u || height < 2u) return;
    gop_fill_rect(x, y, width, height, fill);
    uint32_t top_left = raised ? colors.highlight : colors.shadow;
    uint32_t bottom_right = raised ? colors.shadow : colors.highlight;
    gop_fill_rect(x, y, width, 1u, top_left);
    gop_fill_rect(x, y, 1u, height, top_left);
    gop_fill_rect(x, y + height - 1u, width, 1u, bottom_right);
    gop_fill_rect(x + width - 1u, y, 1u, height, bottom_right);
}

static void load_colors(void) {
    colors.field = gop_rgb(0x4c, 0x51, 0x48);
    colors.chrome = gop_rgb(0xb8, 0xb4, 0xa5);
    colors.highlight = gop_rgb(0xd3, 0xd0, 0xc2);
    colors.shadow = gop_rgb(0x6e, 0x6c, 0x64);
    colors.terminal = gop_rgb(0x17, 0x1a, 0x17);
    colors.terminal_text = gop_rgb(0xc9, 0xc8, 0xb5);
    colors.title = gop_rgb(0x4e, 0x58, 0x69);
    colors.ink = gop_rgb(0x24, 0x27, 0x24);
}

static void draw_window(void) {
    draw_bevel(window_x, window_y, window_width, window_height, colors.chrome, 1);
    gop_fill_rect(
        window_x + 3u,
        window_y + 3u,
        window_width - 6u,
        title_height,
        colors.title
    );
    draw_text(
        "KERNEL CONSOLE",
        window_x + 7u,
        window_y + 5u,
        scale,
        colors.terminal_text
    );

    uint32_t terminal_x = window_x + terminal_offset_x;
    uint32_t terminal_y = window_y + terminal_offset_y;
    draw_bevel(
        terminal_x,
        terminal_y,
        terminal_width,
        terminal_height,
        colors.terminal,
        0
    );
}

static void draw_panel(const argus_gop_t *g) {
    uint32_t panel_y = g->height - panel_height;
    gop_fill_rect(0, panel_y, g->width, panel_height, colors.chrome);
    gop_fill_rect(0, panel_y, g->width, 1u, colors.highlight);
    gop_fill_rect(0, g->height - 1u, g->width, 1u, colors.shadow);

    const char *task = "KERNEL CONSOLE";
    uint32_t task_width = text_width(task, scale) + 16u;
    uint32_t task_height = panel_height - 8u;
    draw_bevel(4u, panel_y + 4u, task_width, task_height, colors.chrome, 0);
    draw_text(
        task,
        12u,
        panel_y + (panel_height - 7u * scale) / 2u,
        scale,
        colors.ink
    );
}

static int point_in_title(uint32_t x, uint32_t y) {
    return x >= window_x + 3u && x < window_x + window_width - 3u &&
           y >= window_y + 3u && y < window_y + 3u + title_height;
}

static void fill_exposed_window_area(
    uint32_t old_x,
    uint32_t old_y,
    uint32_t new_x,
    uint32_t new_y
) {
    uint32_t old_right = old_x + window_width;
    uint32_t old_bottom = old_y + window_height;
    uint32_t new_right = new_x + window_width;
    uint32_t new_bottom = new_y + window_height;
    uint32_t overlap_left = old_x > new_x ? old_x : new_x;
    uint32_t overlap_top = old_y > new_y ? old_y : new_y;
    uint32_t overlap_right = old_right < new_right ? old_right : new_right;
    uint32_t overlap_bottom = old_bottom < new_bottom ? old_bottom : new_bottom;

    if (overlap_left >= overlap_right || overlap_top >= overlap_bottom) {
        gop_fill_rect(old_x, old_y, window_width, window_height, colors.field);
        return;
    }

    gop_fill_rect(old_x, old_y, window_width, overlap_top - old_y, colors.field);
    gop_fill_rect(
        old_x,
        overlap_bottom,
        window_width,
        old_bottom - overlap_bottom,
        colors.field
    );
    gop_fill_rect(
        old_x,
        overlap_top,
        overlap_left - old_x,
        overlap_bottom - overlap_top,
        colors.field
    );
    gop_fill_rect(
        overlap_right,
        overlap_top,
        old_right - overlap_right,
        overlap_bottom - overlap_top,
        colors.field
    );
}

static void move_window(uint32_t new_x, uint32_t new_y) {
    const argus_gop_t *g = gop_info();
    uint32_t maximum_x = g->width - window_width;
    uint32_t maximum_y = g->height - panel_height - window_height;
    if (new_x > maximum_x) new_x = maximum_x;
    if (new_y > maximum_y) new_y = maximum_y;
    if (new_x == window_x && new_y == window_y) return;

    uint32_t old_x = window_x;
    uint32_t old_y = window_y;
    if (!gop_move_rect(
            old_x,
            old_y,
            new_x,
            new_y,
            window_width,
            window_height))
        return;
    fill_exposed_window_area(old_x, old_y, new_x, new_y);
    window_x = new_x;
    window_y = new_y;
    (void)console_move_region(
        window_x + terminal_offset_x + 3u,
        window_y + terminal_offset_y + 3u
    );
    ++drag_moves;
}

static uint32_t clamp_pointer_coordinate(int64_t value, uint32_t maximum) {
    if (value < 0) return 0;
    if ((uint64_t)value > maximum) return maximum;
    return (uint32_t)value;
}

int desktop_redraw(void) {
    if (!console_uses_framebuffer()) return 0;
    const argus_gop_t *g = gop_info();
    if (!g->usable || g->width < 320u || g->height < 200u) return 0;

    pointer_visible = 0;
    dragging = 0;
    pointer_buttons = 0;
    load_colors();
    scale = g->width >= 800u && g->height >= 600u ? 2u : 1u;
    panel_height = scale == 2u ? 32u : 24u;
    title_height = scale == 2u ? 24u : 16u;
    uint32_t available_height = g->height - panel_height;
    window_width = (g->width * 3u) / 4u;
    window_height = (available_height * 3u) / 4u;
    window_x = (g->width - window_width) / 2u;
    window_y = (available_height - window_height) / 2u;
    terminal_offset_x = 4u;
    terminal_offset_y = title_height + 5u;
    terminal_width = window_width - 8u;
    terminal_height = window_height - title_height - 9u;

    gop_fill(colors.field);
    draw_window();
    draw_panel(g);

    if (!pointer_initialized) {
        pointer_x = g->width / 2u;
        pointer_y = available_height / 2u;
        pointer_initialized = 1;
    }

    return console_set_region(
        window_x + terminal_offset_x + 3u,
        window_y + terminal_offset_y + 3u,
        terminal_width - 6u,
        terminal_height - 6u,
        0xc9,
        0xc8,
        0xb5,
        0x17,
        0x1a,
        0x17
    );
}

int desktop_init(void) {
    pointer_initialized = 0;
    pointer_enabled = 0;
    pointer_visible = 0;
    dragging = 0;
    pointer_buttons = 0;
    drag_moves = 0;
    return desktop_redraw();
}

void desktop_pointer_set_enabled(int enabled) {
    if (!enabled) desktop_pointer_hide();
    pointer_enabled = enabled != 0;
}

void desktop_pointer_show(void) {
    if (!pointer_enabled || pointer_visible || !console_uses_framebuffer()) return;
    const argus_gop_t *g = gop_info();
    pointer_saved_width = POINTER_WIDTH;
    pointer_saved_height = POINTER_HEIGHT;
    if (pointer_saved_width > g->width - pointer_x)
        pointer_saved_width = g->width - pointer_x;
    if (pointer_saved_height > g->height - pointer_y)
        pointer_saved_height = g->height - pointer_y;

    for (uint32_t y = 0; y < pointer_saved_height; ++y) {
        for (uint32_t x = 0; x < pointer_saved_width; ++x) {
            pointer_saved[y * POINTER_WIDTH + x] =
                gop_getpixel(pointer_x + x, pointer_y + y);
            uint16_t bit = (uint16_t)(1u << (11u - x));
            if (pointer_outer[y] & bit) {
                uint32_t color = pointer_inner[y] & bit
                    ? colors.highlight
                    : colors.ink;
                gop_putpixel(pointer_x + x, pointer_y + y, color);
            }
        }
    }
    pointer_visible = 1;
}

void desktop_pointer_hide(void) {
    if (!pointer_visible) return;
    for (uint32_t y = 0; y < pointer_saved_height; ++y)
        for (uint32_t x = 0; x < pointer_saved_width; ++x)
            gop_putpixel(
                pointer_x + x,
                pointer_y + y,
                pointer_saved[y * POINTER_WIDTH + x]
            );
    pointer_visible = 0;
}

void desktop_pointer_event(int16_t dx, int16_t dy, uint8_t buttons) {
    if (!pointer_enabled || !console_uses_framebuffer()) return;
    const argus_gop_t *g = gop_info();
    desktop_pointer_hide();
    pointer_x = clamp_pointer_coordinate(
        (int64_t)pointer_x + dx,
        g->width - 1u
    );
    pointer_y = clamp_pointer_coordinate(
        (int64_t)pointer_y + dy,
        g->height - 1u
    );

    int left_pressed = (buttons & 1u) && !(pointer_buttons & 1u);
    if (left_pressed && point_in_title(pointer_x, pointer_y)) {
        dragging = 1;
        drag_offset_x = pointer_x - window_x;
        drag_offset_y = pointer_y - window_y;
    }
    if (dragging && (buttons & 1u)) {
        uint32_t new_x = pointer_x > drag_offset_x
            ? pointer_x - drag_offset_x
            : 0u;
        uint32_t new_y = pointer_y > drag_offset_y
            ? pointer_y - drag_offset_y
            : 0u;
        move_window(new_x, new_y);
    }
    if (!(buttons & 1u)) dragging = 0;
    pointer_buttons = buttons;
    desktop_pointer_show();
}

uint64_t desktop_drag_moves(void) { return drag_moves; }
uint32_t desktop_pointer_x(void) { return pointer_x; }
uint32_t desktop_pointer_y(void) { return pointer_y; }
uint32_t desktop_window_x(void) { return window_x; }
uint32_t desktop_window_y(void) { return window_y; }
