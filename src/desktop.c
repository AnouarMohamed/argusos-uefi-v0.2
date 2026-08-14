#include "desktop.h"
#include "console.h"
#include "font5x7.h"
#include "gop.h"

typedef struct {
    uint32_t field;
    uint32_t field_dark;
    uint32_t chrome;
    uint32_t highlight;
    uint32_t shadow;
    uint32_t terminal;
    uint32_t terminal_text;
    uint32_t title;
    uint32_t ink;
} desktop_colors_t;

static desktop_colors_t colors;

static uint32_t text_width(const char *text, uint32_t scale) {
    uint32_t count = 0;
    while (*text++) ++count;
    return count * 6u * scale;
}

static void draw_glyph(char c, uint32_t x, uint32_t y, uint32_t scale, uint32_t color) {
    for (uint32_t row = 0; row < 7u; ++row) {
        uint8_t bits = font5x7_row(c, row);
        for (uint32_t col = 0; col < 5u; ++col) {
            if (bits & (1u << (4u - col)))
                gop_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static void draw_text(const char *text, uint32_t x, uint32_t y, uint32_t scale, uint32_t color) {
    while (*text) {
        draw_glyph(*text++, x, y, scale, color);
        x += 6u * scale;
    }
}

static void draw_centered_text(
    const char *text,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t scale,
    uint32_t color
) {
    uint32_t width_used = text_width(text, scale);
    uint32_t start = width_used < width ? x + (width - width_used) / 2u : x;
    draw_text(text, start, y, scale, color);
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

static void draw_icon(
    uint32_t x,
    uint32_t y,
    const char *symbol,
    const char *label,
    uint32_t scale
) {
    uint32_t box_width = 34u * scale;
    uint32_t box_height = 24u * scale;
    draw_bevel(x, y, box_width, box_height, colors.chrome, 1);
    draw_centered_text(
        symbol,
        x,
        y + (box_height - 7u * scale) / 2u,
        box_width,
        scale,
        colors.ink
    );
    draw_centered_text(
        label,
        x > 8u * scale ? x - 8u * scale : 0u,
        y + box_height + 5u * scale,
        box_width + 16u * scale,
        scale,
        colors.terminal_text
    );
}

static void load_colors(void) {
    colors.field = gop_rgb(0x4c, 0x51, 0x48);
    colors.field_dark = gop_rgb(0x41, 0x46, 0x3f);
    colors.chrome = gop_rgb(0xb8, 0xb4, 0xa5);
    colors.highlight = gop_rgb(0xd3, 0xd0, 0xc2);
    colors.shadow = gop_rgb(0x6e, 0x6c, 0x64);
    colors.terminal = gop_rgb(0x17, 0x1a, 0x17);
    colors.terminal_text = gop_rgb(0xc9, 0xc8, 0xb5);
    colors.title = gop_rgb(0x4e, 0x58, 0x69);
    colors.ink = gop_rgb(0x24, 0x27, 0x24);
}

int desktop_redraw(void) {
    if (!console_uses_framebuffer()) return 0;
    const argus_gop_t *g = gop_info();
    if (!g->usable || g->width < 320u || g->height < 200u) return 0;

    load_colors();
    uint32_t scale = g->width >= 800u && g->height >= 600u ? 2u : 1u;
    uint32_t panel_height = scale == 2u ? 32u : 24u;
    uint32_t left_strip = scale == 2u ? 116u : 82u;
    uint32_t outer_margin = scale == 2u ? 20u : 12u;
    uint32_t window_y = scale == 2u ? 54u : 40u;
    uint32_t title_height = scale == 2u ? 24u : 16u;
    uint32_t window_width = g->width - left_strip - outer_margin;
    uint32_t window_bottom = g->height - panel_height - outer_margin;
    if (window_bottom <= window_y + title_height + 12u) return 0;
    uint32_t window_height = window_bottom - window_y;

    gop_fill(colors.field);
    gop_fill_rect(0, 0, g->width, 1u, colors.field_dark);
    draw_text("ARGUS 9OS // LOCAL WORKSTATION", 10u, 10u, scale, colors.terminal_text);
    gop_fill_rect(10u, 20u * scale, left_strip - 20u, 1u, colors.field_dark);

    uint32_t icon_x = scale == 2u ? 24u : 14u;
    uint32_t icon_y = scale == 2u ? 64u : 48u;
    draw_icon(icon_x, icon_y, ">_", "TTY0", scale);
    if (icon_y + 112u * scale < g->height - panel_height)
        draw_icon(icon_x, icon_y + 62u * scale, "/", "FILES", scale);
    if (icon_y + 174u * scale < g->height - panel_height)
        draw_icon(icon_x, icon_y + 124u * scale, "I", "ABOUT", scale);

    draw_bevel(left_strip, window_y, window_width, window_height, colors.chrome, 1);
    gop_fill_rect(
        left_strip + 3u,
        window_y + 3u,
        window_width - 6u,
        title_height,
        colors.title
    );
    draw_text(
        "ARGUS KERNEL CONSOLE // TTY0",
        left_strip + 7u,
        window_y + 5u,
        scale,
        colors.terminal_text
    );

    uint32_t terminal_x = left_strip + 4u;
    uint32_t terminal_y = window_y + title_height + 5u;
    uint32_t terminal_width = window_width - 8u;
    uint32_t terminal_height = window_height - title_height - 9u;
    draw_bevel(
        terminal_x,
        terminal_y,
        terminal_width,
        terminal_height,
        colors.terminal,
        0
    );

    uint32_t panel_y = g->height - panel_height;
    gop_fill_rect(0, panel_y, g->width, panel_height, colors.chrome);
    gop_fill_rect(0, panel_y, g->width, 1u, colors.highlight);
    gop_fill_rect(0, g->height - 1u, g->width, 1u, colors.shadow);

    uint32_t button_height = panel_height - 8u;
    uint32_t launcher_width = scale == 2u ? 104u : 72u;
    draw_bevel(4u, panel_y + 4u, launcher_width, button_height, colors.chrome, 1);
    draw_centered_text(
        "ARGUS",
        4u,
        panel_y + (panel_height - 7u * scale) / 2u,
        launcher_width,
        scale,
        colors.ink
    );

    uint32_t task_x = launcher_width + 10u;
    uint32_t task_width = scale == 2u ? 248u : 146u;
    if (task_x + task_width + 8u < g->width) {
        draw_bevel(task_x, panel_y + 4u, task_width, button_height, colors.chrome, 0);
        draw_text(
            scale == 2u ? "TTY0: KERNEL CONSOLE" : "TTY0: CONSOLE",
            task_x + 7u,
            panel_y + (panel_height - 7u * scale) / 2u,
            scale,
            colors.ink
        );
    }

    const char *status = scale == 2u ? "PS/2 | LOCAL" : "LOCAL";
    uint32_t status_width = text_width(status, scale) + 12u;
    if (status_width + 4u < g->width) {
        uint32_t status_x = g->width - status_width - 4u;
        draw_bevel(status_x, panel_y + 4u, status_width, button_height, colors.chrome, 0);
        draw_centered_text(
            status,
            status_x,
            panel_y + (panel_height - 7u * scale) / 2u,
            status_width,
            scale,
            colors.ink
        );
    }

    return console_set_region(
        terminal_x + 3u,
        terminal_y + 3u,
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
    return desktop_redraw();
}
